# FSEm — Consola de videojuegos retro (Raspberry Pi 4 + Buildroot)

Proyecto final de **Fundamentos de Sistemas Embebidos** (Facultad de Ingeniería, UNAM).

Consola de videojuegos retro que emula **NES, SNES y Game Boy Advance** sobre una
Raspberry Pi 4. El sistema es una distribución Linux mínima de 64 bits generada con
**Buildroot** (sin escritorio ni gestor de ventanas): al conectar la alimentación
muestra una pantalla de carga propia con logotipo y sonido y arranca directamente
en una interfaz operada únicamente con gamepad.

## Autores

| Nombre |
|---|
| Adler Luis Castillo |
| Rafael Campos Camargo |

## Enlaces

- **Video demostrativo:** <https://youtu.be/g-huY0UPm0I>
- **Repositorio:** <https://github.com/luismenorax/Proyecto-Final-FSEmb>

```bash
git clone https://github.com/luismenorax/Proyecto-Final-FSEmb.git
cd Proyecto-Final-FSEmb
```

## Características

- Distribución mínima con **Buildroot 2026.08.2** (kernel Raspberry Pi 6.12.61-v8, BusyBox init y mdev).
- **Pantalla de carga** propia (logotipo con fade-in, barra de progreso y melodía sintetizada).
- Arranque directo a la interfaz, sin teclado ni mouse.
- Interfaz en **C y SDL2** (backend KMSDRM) con menú de sistemas y bibliotecas de ROM.
- Emuladores: **mGBA** (GBA) y **Mednafen 1.32.1** (NES y SNES).
- Gráficos acelerados con **VC4/V3D + Mesa**; audio con **ALSA** (48 kHz, estéreo, 16 bits).
- **Importación automática de ROM desde USB** (mdev), sin duplicados y con copia verificada.
- Soporte de mando Xbox Series S|X: navegación por botones, cruceta y stick izquierdo.

## Estructura del entregable

```
doc/            Tutorial (reporte escrito) y documentos técnicos
src/
  interfaz/     main.c          Interfaz SDL2 (menú, bibliotecas, lanzamiento de emuladores)
  splash/       splash.c        Pantalla de carga con sonido
  usb/          rom-usb-import.sh   Importación de ROM desde USB
  tools/        joystick.c      Diagnóstico del control (GUID, botones, ejes)
  buildroot/    fsem_raspberrypi4_defconfig y archivos de board (overlay, config.txt, fragmento de kernel)
vid/            Enlace al video demostrativo
build.sh        Compilación automática de todo el proyecto y generación de la imagen
LICENSE         Licencia MIT
readme.MD       Este archivo
```

## Requisitos

**Hardware:** Raspberry Pi 4 Model B, microSD (16 GB o más), fuente USB-C 5 V/3 A,
monitor o TV HDMI (el audio sale por HDMI), gamepad Xbox Series S|X y memoria USB (FAT)
con ROM.

**Equipo de compilación:** Linux (no máquina virtual) con las dependencias de Buildroot:

```bash
sudo apt install -y build-essential git libncurses-dev rsync wget cpio unzip bc file
```

## Imagen lista para grabar

Por su tamaño, la imagen no se incluye en el archivo comprimido del entregable:

- **Descargar `sdcard.tar.xz` (60.6 MB):** <https://drive.google.com/file/d/1_sUsdZnyIM_OBg7NA-MP80DaNaQAcw4l/view?usp=sharing>

> **Importante:** el archivo `sdcard.tar.xz` **sólo puede descomprimirse en Linux**
> (no en Windows ni en macOS). Descomprimirlo en un equipo Linux antes de grabarlo:

```bash
tar -xJf sdcard.tar.xz      # genera sdcard.img
```

Después, grabar `sdcard.img` en la microSD con **balenaEtcher** (seleccionar la imagen,
**verificar la unidad destino** y *Flash*), insertar la tarjeta y alimentar la Raspberry Pi.

## Compilación automatizada

El script `build.sh` descarga Buildroot 2026.08.2, aplica la configuración del proyecto,
compila la interfaz y la pantalla de carga, y genera la imagen completa:

```bash
git clone https://github.com/luismenorax/Proyecto-Final-FSEmb.git
cd Proyecto-Final-FSEmb
./build.sh --deps      # --deps instala las dependencias del host (Debian/Ubuntu, usa sudo)
```

El resultado queda en `output/sdcard.img`. La primera compilación puede tardar
varias horas y requiere conexión a Internet y decenas de GB libres.

## Uso

1. Conectar monitor HDMI y gamepad, y alimentar la placa. Se muestra la pantalla de carga y después el menú.
2. **Cruceta / stick izquierdo:** mover la selección. **A:** elegir. **B:** regresar.
3. Seleccionar el sistema (NES, SNES o GBA) y el juego.
4. **Botón central (mantener 2 s):** cerrar el juego y volver a la biblioteca.

### Importar ROM desde USB

Preparar la memoria USB (FAT) con esta estructura y conectarla a la consola:

```
roms/
  NES/    *.nes
  SNES/   *.sfc  *.smc
  GBA/    *.gba
```

El script copia a `/home/pi/roms/{nes,snes,gba}`, omite duplicados por contenido
y registra el resultado en `/home/pi/roms/usb-import.log`. La biblioteca se actualiza sola.

Utilizar únicamente ROM de uso permitido (copias de respaldo propias o *homebrew*).

## Limitaciones conocidas

- La cruceta física del mando no funciona dentro de Mednafen (sí en la interfaz); se usa el stick izquierdo.
- La interfaz lista únicamente archivos `.gba` aunque el script también copie `.gb`/`.gbc`.

## Licencia

Distribuido bajo licencia **MIT**; véase el archivo [LICENSE](LICENSE).
Mednafen, mGBA, SDL2, Mesa y Buildroot conservan sus licencias originales.
