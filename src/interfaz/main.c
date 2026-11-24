#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <SDL.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define FONT_SCALE 4
#define ROM_DIR "/home/pi/roms/gba"
#define MAX_ROMS 512
#define VISIBLE_ROMS 9

static const unsigned char font[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {15,16,16,16,16,16,15}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {15,16,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {31,4,4,4,4,4,31}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Joystick *joystick;

static char *roms[MAX_ROMS];
static int rom_count = 0;

static void draw_text(SDL_Renderer *r, int x, int y,
                      const char *text, SDL_Color color, int scale)
{
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);

    for (const char *p = text; *p; p++) {
        char c = (char)toupper((unsigned char)*p);

        if (c >= 'A' && c <= 'Z') {
            const unsigned char *glyph = font[c - 'A'];

            for (int row = 0; row < 7; row++) {
                for (int col = 0; col < 5; col++) {
                    if (glyph[row] & (1 << (4 - col))) {
                        SDL_Rect pixel = {
                            x + col * scale,
                            y + row * scale,
                            scale,
                            scale
                        };
                        SDL_RenderFillRect(r, &pixel);
                    }
                }
            }
        }

        x += 6 * scale;
    }
}

static void draw_centered(SDL_Renderer *r, int y, const char *text,
                          SDL_Color color, int scale)
{
    int width = (int)strlen(text) * 6 * scale - scale;

    draw_text(r, (SCREEN_W - width) / 2, y, text, color, scale);
}

static void draw_card(SDL_Renderer *r, int x, int selected,
                      const char *label)
{
    SDL_Rect card = {x, 260, 280, 210};

    if (selected)
        SDL_SetRenderDrawColor(r, 35, 115, 190, 255);
    else
        SDL_SetRenderDrawColor(r, 38, 45, 58, 255);

    SDL_RenderFillRect(r, &card);

    SDL_SetRenderDrawColor(r, 90, 105, 125, 255);
    SDL_RenderDrawRect(r, &card);

    int text_width = (int)strlen(label) * 6 * FONT_SCALE - FONT_SCALE;

    draw_text(r, x + (280 - text_width) / 2, 345, label,
              (SDL_Color){255, 255, 255, 255}, FONT_SCALE);
}

static int init_sdl(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "Error SDL: %s\n", SDL_GetError());
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window = SDL_CreateWindow(
        "Consola Retro",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_W,
        SCREEN_H,
        SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        fprintf(stderr, "Error de ventana: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    if (!renderer) {
        fprintf(stderr, "Error de renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
    SDL_JoystickEventState(SDL_ENABLE);

    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);

        if (joystick) {
            printf("Control detectado: %s\n",
                   SDL_JoystickName(joystick));
        }
    }

    return 0;
}

static void close_sdl(void)
{
    if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    SDL_Quit();
}

static int is_gba_file(const char *name)
{
    size_t len = strlen(name);

    return len > 4 && strcasecmp(name + len - 4, ".gba") == 0;
}

static int compare_roms(const void *a, const void *b)
{
    return strcasecmp(*(char * const *)a, *(char * const *)b);
}

static void free_roms(void)
{
    for (int i = 0; i < rom_count; i++)
        free(roms[i]);

    rom_count = 0;
}

static void scan_roms(void)
{
    DIR *dir;
    struct dirent *entry;

    free_roms();

    dir = opendir(ROM_DIR);

    if (!dir) {
        perror("No se pudo abrir el directorio de ROMs");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!is_gba_file(entry->d_name))
            continue;

        if (rom_count >= MAX_ROMS)
            break;

        roms[rom_count] = strdup(entry->d_name);

        if (roms[rom_count])
            rom_count++;
    }

    closedir(dir);

    qsort(roms, rom_count, sizeof(roms[0]), compare_roms);

    printf("ROMs GBA encontradas: %d\n", rom_count);
}

static int launch_game(const char *filename)
{
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/%s",
                 ROM_DIR, filename) >= (int)sizeof(path)) {
        fprintf(stderr, "Ruta de ROM demasiado larga.\n");
        return -1;
    }

    printf("Iniciando: %s\n", path);

    /*
     * Liberar SDL antes de iniciar mGBA.
     * Ambos programas utilizan SDL y la salida de video.
     */
    close_sdl();

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        init_sdl();
        return -1;
    }

    if (pid == 0) {
        setenv("HOME", "/root", 1);

        execl("/usr/bin/mgba", "mgba", path, (char *)NULL);
        perror("No se pudo ejecutar mGBA");
        _exit(127);
    }

    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }

    /*
     * Recuperar SDL y el control al cerrar el emulador.
     */
    if (init_sdl() < 0)
        return -1;

    return 0;
}

static void draw_library(int selected, int scroll)
{
    draw_centered(renderer, 55, "BIBLIOTECA GBA",
                  (SDL_Color){255, 255, 255, 255}, 5);

    if (rom_count == 0) {
        draw_centered(renderer, 300, "NO HAY JUEGOS GBA",
                      (SDL_Color){180, 195, 215, 255}, 4);

        draw_centered(renderer, 380, "COPIA ROMS A LA CARPETA GBA",
                      (SDL_Color){160, 175, 195, 255}, 2);
        return;
    }

    for (int i = 0; i < VISIBLE_ROMS; i++) {
        int index = scroll + i;

        if (index >= rom_count)
            break;

        SDL_Rect row = {130, 160 + i * 48, 1020, 42};

        if (index == selected)
            SDL_SetRenderDrawColor(renderer, 35, 115, 190, 255);
        else
            SDL_SetRenderDrawColor(renderer, 38, 45, 58, 255);

        SDL_RenderFillRect(renderer, &row);

        draw_text(renderer, 150, 173 + i * 48,
                  roms[index],
                  (SDL_Color){255, 255, 255, 255}, 2);
    }

    draw_centered(renderer, 635, "A ELEGIR   B REGRESAR",
                  (SDL_Color){160, 175, 195, 255}, 2);
}

int main(void)
{
    const char *systems[] = {"NES", "SNES", "GBA"};

    int running = 1;
    int selected = 0;
    int screen = 0;
    int library_selected = 0;
    int library_scroll = 0;

    Uint32 last_navigation = 0;

    if (init_sdl() < 0)
        return 1;

    printf("Interfaz SDL2 iniciada.\n");

    while (running) {
        SDL_Event event;
        Uint32 now = SDL_GetTicks();
        int direction = 0;
        int action = 0;
        int back = 0;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                running = 0;

            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        running = 0;
                        break;

                    case SDLK_LEFT:
                        if (screen == 0)
                            direction = -1;
                        break;

                    case SDLK_RIGHT:
                        if (screen == 0)
                            direction = 1;
                        break;

                    case SDLK_UP:
                        direction = -1;
                        break;

                    case SDLK_DOWN:
                        direction = 1;
                        break;

                    case SDLK_RETURN:
                    case SDLK_SPACE:
                        action = 1;
                        break;

                    case SDLK_BACKSPACE:
                        back = 1;
                        break;
                }
            }

            if (event.type == SDL_JOYBUTTONDOWN) {
                if (event.jbutton.button == 0)
                    action = 1;

                if (event.jbutton.button == 1)
                    back = 1;
            }

            if (event.type == SDL_JOYHATMOTION) {
                if (screen == 0) {
                    if (event.jhat.value & SDL_HAT_LEFT)
                        direction = -1;
                    else if (event.jhat.value & SDL_HAT_RIGHT)
                        direction = 1;
                } else {
                    if (event.jhat.value & SDL_HAT_UP)
                        direction = -1;
                    else if (event.jhat.value & SDL_HAT_DOWN)
                        direction = 1;
                }
            }
        }

        /* Lectura de ejes analogicos */
        if (joystick && SDL_JoystickNumAxes(joystick) >= 2) {
            Sint16 x = SDL_JoystickGetAxis(joystick, 0);
            Sint16 y = SDL_JoystickGetAxis(joystick, 1);

            if (screen == 0) {
                if (x < -16000)
                    direction = -1;
                else if (x > 16000)
                    direction = 1;
            } else {
                if (y < -16000)
                    direction = -1;
                else if (y > 16000)
                    direction = 1;
            }
        }

        /* Navegacion */
        if (direction != 0 && now - last_navigation > 220) {
            if (screen == 0) {
                selected = (selected + direction + 3) % 3;
            } else if (screen == 1 && rom_count > 0) {
                library_selected =
                    (library_selected + direction + rom_count) % rom_count;

                if (library_selected < library_scroll)
                    library_scroll = library_selected;

                if (library_selected >= library_scroll + VISIBLE_ROMS)
                    library_scroll = library_selected - VISIBLE_ROMS + 1;
            }

            last_navigation = now;
        }

        /* Seleccion */
        if (action) {
            if (screen == 0) {
                if (selected == 2) {
                    scan_roms();
                    library_selected = 0;
                    library_scroll = 0;
                    screen = 1;
                } else {
                    screen = 2;
                }
            } else if (screen == 1 && rom_count > 0) {
                char *game = strdup(roms[library_selected]);

                if (game) {
                    if (launch_game(game) < 0) {
                        free(game);
                        running = 0;
                        continue;
                    }

                    free(game);
                }
            }
        }

        if (back && screen != 0)
            screen = 0;

        /* Dibujar */
        SDL_SetRenderDrawColor(renderer, 17, 22, 32, 255);
        SDL_RenderClear(renderer);

        if (screen == 0) {
            draw_centered(renderer, 105, "SELECCIONA SISTEMA",
                          (SDL_Color){255, 255, 255, 255}, 5);

            draw_card(renderer, 110, selected == 0, systems[0]);
            draw_card(renderer, 500, selected == 1, systems[1]);
            draw_card(renderer, 890, selected == 2, systems[2]);

            draw_centered(renderer, 560, "MOVER",
                          (SDL_Color){160, 175, 195, 255}, 3);

            draw_centered(renderer, 610, "ELEGIR",
                          (SDL_Color){160, 175, 195, 255}, 3);

        } else if (screen == 1) {
            draw_library(library_selected, library_scroll);

        } else {
            draw_centered(renderer, 160, systems[selected],
                          (SDL_Color){255, 255, 255, 255}, 7);

            draw_centered(renderer, 320, "NO DISPONIBLE",
                          (SDL_Color){180, 195, 215, 255}, 4);

            draw_centered(renderer, 500, "REGRESAR",
                          (SDL_Color){100, 190, 255, 255}, 4);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    close_sdl();
    free_roms();

    return 0;
}
