#!/bin/sh

USB_MOUNT="/mnt/usb"
ROM_BASE="/home/pi/roms"
LOCK="/tmp/rom-usb-import.lock"
LOG="/home/pi/roms/usb-import.log"

IMPORTED=0
DUPLICATES=0
MOUNTED=0
LIST=""

log() {
    echo "[rom-usb-import] $*" >> "$LOG"
}

cleanup() {
    if [ -n "$LIST" ]; then
        rm -f "$LIST"
    fi

    if [ "$MOUNTED" -eq 1 ]; then
        umount "$USB_MOUNT"
        log "Unidad desmontada."
    fi

    rmdir "$LOCK" 2>/dev/null
}

# Entrada desde mdev: registrar el evento antes de validar el dispositivo.
if [ "$1" != "--worker" ]; then
    mkdir -p "$ROM_BASE"
    log "Invocación: ACTION=${ACTION:-vacio} SUBSYSTEM=${SUBSYSTEM:-vacio} MDEV=${MDEV:-vacio}"

    DEV="$MDEV"

    case "$DEV" in
        sd[a-z]|sd[a-z][0-9]*) ;;
        *) exit 0 ;;
    esac

    "$0" --worker "$DEV" </dev/null >> "$LOG" 2>&1 &
    exit 0
fi

DEV="$2"
log "Iniciando importación para $DEV."

case "$DEV" in
    sd[a-z]|sd[a-z][0-9]*) ;;
    *) exit 0 ;;
esac

DEVICE="/dev/$DEV"
DISK="${DEV%%[0-9]*}"
REMOVABLE="/sys/block/$DISK/removable"

# Esperar a que el dispositivo y su atributo removable estén disponibles.
COUNT=0
while [ "$COUNT" -lt 15 ]; do
    if [ -b "$DEVICE" ] && [ -f "$REMOVABLE" ]; then
        break
    fi

    sleep 1
    COUNT=$((COUNT + 1))
done

if [ ! -b "$DEVICE" ] || [ ! -f "$REMOVABLE" ]; then
    log "Dispositivo $DEV no disponible."
    exit 1
fi

if [ "$(cat "$REMOVABLE")" != "1" ]; then
    log "$DEV no es extraíble."
    exit 0
fi

# Bloqueo global para evitar importaciones simultáneas.
if ! mkdir "$LOCK" 2>/dev/null; then
    log "Ya existe otra importación en ejecución."
    exit 1
fi

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Si mdev notificó el disco completo, esperar a que aparezca una partición.
if [ "$DEV" = "$DISK" ]; then
    COUNT=0
    PARTITION=""

    while [ "$COUNT" -lt 15 ]; do
        for PART_PATH in /sys/block/"$DISK"/"$DISK"[0-9]*; do
            [ -e "$PART_PATH" ] || continue

            CANDIDATE="${PART_PATH##*/}"

            if [ -b "/dev/$CANDIDATE" ]; then
                PARTITION="$CANDIDATE"
                break
            fi
        done

        [ -n "$PARTITION" ] && break

        sleep 1
        COUNT=$((COUNT + 1))
    done

    if [ -n "$PARTITION" ]; then
        DEV="$PARTITION"
        DEVICE="/dev/$DEV"
        log "Partición detectada: $DEVICE"
    else
        log "No se detectaron particiones; se intentará montar $DEVICE."
    fi
fi

mkdir -p "$USB_MOUNT"

# Montaje de solo lectura con autodetección.
if ! mount -o ro "$DEVICE" "$USB_MOUNT"; then
    log "No se pudo montar $DEVICE."
    exit 1
fi

MOUNTED=1
log "Unidad $DEVICE montada."

import_rom() {
    SOURCE="$1"
    CATEGORY="$2"

    EXT="${SOURCE##*.}"
    EXT="$(echo "$EXT" | tr '[:upper:]' '[:lower:]')"

    case "$CATEGORY:$EXT" in
        gba:gba|gba:gbc|gba:gb) ;;
        nes:nes) ;;
        snes:sfc|snes:smc) ;;
        *) return ;;
    esac

    DEST_DIR="$ROM_BASE/$CATEGORY"
    mkdir -p "$DEST_DIR"

    HASH="$(sha256sum "$SOURCE" | cut -d ' ' -f 1)"

    if [ -z "$HASH" ]; then
        log "No se pudo calcular SHA-256: $SOURCE"
        return
    fi

    # Evitar duplicados por contenido.
    for EXISTING in "$DEST_DIR"/*; do
        [ -f "$EXISTING" ] || continue

        EXISTING_HASH="$(sha256sum "$EXISTING" | cut -d ' ' -f 1)"

        if [ "$HASH" = "$EXISTING_HASH" ]; then
            log "Duplicado omitido: $SOURCE"
            DUPLICATES=$((DUPLICATES + 1))
            return
        fi
    done

    NAME="$(basename "$SOURCE")"
    DEST="$DEST_DIR/$NAME"

    # No sobrescribir archivos con contenido diferente.
    if [ -e "$DEST" ]; then
        STEM="${NAME%.*}"
        DEST="$DEST_DIR/$STEM-$HASH.$EXT"

        INDEX=1
        while [ -e "$DEST" ]; do
            DEST="$DEST_DIR/$STEM-$HASH-$INDEX.$EXT"
            INDEX=$((INDEX + 1))
        done
    fi

    TEMP="$DEST.tmp"

    if cp "$SOURCE" "$TEMP" &&
       [ "$(sha256sum "$TEMP" | cut -d ' ' -f 1)" = "$HASH" ]; then
        mv "$TEMP" "$DEST"
        log "ROM importada: $DEST"
        IMPORTED=$((IMPORTED + 1))
    else
        rm -f "$TEMP"
        log "Error al copiar: $SOURCE"
    fi
}

# Recorrer las categorías sin utilizar tuberías para conservar los contadores.
for CATEGORY in GBA NES SNES; do
    SOURCE_DIR="$USB_MOUNT/roms/$CATEGORY"

    [ -d "$SOURCE_DIR" ] || continue

    case "$CATEGORY" in
        GBA) DEST_CATEGORY="gba" ;;
        NES) DEST_CATEGORY="nes" ;;
        SNES) DEST_CATEGORY="snes" ;;
    esac

    LIST="/tmp/rom-usb-import-$DEV-$CATEGORY.list"

    find "$SOURCE_DIR" -type f > "$LIST"

    while IFS= read -r FILE; do
        [ -f "$FILE" ] || continue
        import_rom "$FILE" "$DEST_CATEGORY"
    done < "$LIST"

    rm -f "$LIST"
    LIST=""
done

sync

log "Importación finalizada. Nuevas: $IMPORTED. Duplicadas: $DUPLICATES."
exit 0
