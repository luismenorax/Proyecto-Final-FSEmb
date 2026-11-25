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
#include <signal.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define FONT_SCALE 4

static const char *system_dirs[] = {
    "/home/pi/roms/nes",
    "/home/pi/roms/snes",
    "/home/pi/roms/gba"
};

static const char *system_names[] = {"NES", "SNES", "GBA"};

#define MAX_ROMS 512
#define VISIBLE_ROMS 9

#define IMPORT_STATUS_FILE "/tmp/rom-usb-import.status"
#define IMPORT_RESULTS_FILE "/tmp/rom-usb-import-results.txt"

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

static const unsigned char digits[10][7] = {
    {14,17,19,21,25,17,14},
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Joystick *joystick;

static void stop_psplash(void)
{
    if (access("/usr/bin/psplash-write", X_OK) == 0) {
        int result = system(
            "/usr/bin/psplash-write \"QUIT\" >/dev/null 2>&1"
        );

        (void)result;
    }
}

static void open_joystick(void)
{
    if (joystick)
        return;

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        joystick = SDL_JoystickOpen(i);

        if (joystick) {
            printf("Control detectado: %s\n",
                   SDL_JoystickName(joystick));
            return;
        }
    }
}

static char *roms[MAX_ROMS];
static int rom_count = 0;

static void draw_text(SDL_Renderer *r, int x, int y,
                      const char *text, SDL_Color color, int scale)
{
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);

    for (const char *p = text; *p; p++) {
        char c = (char)toupper((unsigned char)*p);
        const unsigned char *glyph = NULL;

        if (c >= 'A' && c <= 'Z') {
            glyph = font[c - 'A'];
        }
        else if (c >= '0' && c <= '9') {
            glyph = digits[c - '0'];
        }

        if (glyph) {
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

static int is_rom_file(const char *name, int system)
{
    size_t len = strlen(name);

    if (system == 0)
        return len > 4 && strcasecmp(name + len - 4, ".nes") == 0;

    if (system == 1)
        return (len > 4 && strcasecmp(name + len - 4, ".sfc") == 0) ||
               (len > 4 && strcasecmp(name + len - 4, ".smc") == 0);

    if (system == 2)
        return len > 4 && strcasecmp(name + len - 4, ".gba") == 0;

    return 0;
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

static void scan_roms(int system)
{
    DIR *dir;
    struct dirent *entry;

    if (system < 0 || system > 2)
        return;

    free_roms();

    dir = opendir(system_dirs[system]);

    if (!dir) {
        perror("No se pudo abrir el directorio de ROMs");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!is_rom_file(entry->d_name, system))
            continue;

        if (rom_count >= MAX_ROMS)
            break;

        roms[rom_count] = strdup(entry->d_name);

        if (roms[rom_count])
            rom_count++;
    }

    closedir(dir);

    qsort(roms, rom_count, sizeof(roms[0]), compare_roms);

    printf("ROMs %s encontradas: %d\n",
           system_names[system], rom_count);
}

static int read_import_status(char *status, size_t size)
{
    FILE *file = fopen(IMPORT_STATUS_FILE, "r");

    if (!file) {
        return 0;
    }

    if (!fgets(status, (int)size, file)) {
        fclose(file);
        return 0;
    }

    fclose(file);

    status[strcspn(status, "\r\n")] = '\0';

    FILE *log = fopen("/home/pi/roms/usb-debug.log", "a");
    if (log) {
        fprintf(log, "Interfaz detectó estado: %s\n", status);
        fclose(log);
    }

    return 1;
}

static void draw_import_screen(int finished, int failed)
{
    SDL_SetRenderDrawColor(renderer, 17, 22, 32, 255);
    SDL_RenderClear(renderer);

    if (!finished) {
        draw_centered(renderer, 190, "IMPORTANDO ROMS",
                      (SDL_Color){255, 255, 255, 255}, 5);
        draw_centered(renderer, 310, "NO RETIRES LA MEMORIA USB",
                      (SDL_Color){180, 195, 215, 255}, 2);
        draw_centered(renderer, 380, "EL JUEGO ESTA EN PAUSA",
                      (SDL_Color){100, 190, 255, 255}, 2);
    } else {
        draw_centered(renderer, 65,
                      failed ? "ERROR EN IMPORTACION" : "ROMS IMPORTADAS",
                      (SDL_Color){255, 255, 255, 255}, 4);

        FILE *file = fopen(IMPORT_RESULTS_FILE, "r");
        char line[PATH_MAX];
        int row = 0;

        if (file) {
            while (row < 8 && fgets(line, sizeof(line), file)) {
                line[strcspn(line, "\r\n")] = '\0';

                if (line[0] == '\0')
                    continue;

                draw_centered(renderer, 150 + row * 43, line,
                              (SDL_Color){180, 220, 255, 255}, 2);
                row++;
            }

            fclose(file);
        }

        if (row == 0) {
            draw_centered(renderer, 250, "NO SE IMPORTARON ROMS NUEVAS",
                          (SDL_Color){180, 195, 215, 255}, 2);
        }

        draw_centered(renderer, 555, "A CONTINUAR JUGANDO",
                      (SDL_Color){100, 220, 140, 255}, 2);
        draw_centered(renderer, 610, "B VOLVER AL MENU",
                      (SDL_Color){100, 190, 255, 255}, 2);
    }

    SDL_RenderPresent(renderer);
}

static int launch_game(const char *filename, int system)
{
    char path[PATH_MAX];

    if (system < 0 || system > 2)
        return -1;

    if (snprintf(path, sizeof(path), "%s/%s",
                 system_dirs[system], filename) >= (int)sizeof(path)) {
        fprintf(stderr, "Ruta de ROM demasiado larga.\n");
        return -1;
    }

    printf("Iniciando: %s\n", path);

    /*
     * Eliminar el estado de una importación anterior antes de iniciar
     * el emulador. El importador publicará STARTED/DONE durante esta partida.
     */
    unlink(IMPORT_STATUS_FILE);
    unlink(IMPORT_RESULTS_FILE);

    close_sdl();

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        if (init_sdl() == 0)
            open_joystick();
        return -1;
    }

    if (pid == 0) {
        if (system == 2) {
            setenv("HOME", "/root", 1);

            execl("/usr/bin/mgba", "mgba", path, (char *)NULL);
            perror("No se pudo ejecutar mGBA");
        } else {
            setenv("HOME", "/home/pi", 1);

            freopen("/home/pi/roms/mednafen.log", "w", stdout);
            freopen("/home/pi/roms/mednafen.log", "a", stderr);

            execl("/usr/bin/mednafen", "mednafen", path,
                  (char *)NULL);
            perror("No se pudo ejecutar Mednafen");
        }

        _exit(127);
    }

    SDL_Joystick *game_joystick = NULL;

    if (SDL_Init(SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "No se pudo inicializar el joystick: %s\n",
                SDL_GetError());
    } else {
        SDL_JoystickEventState(SDL_ENABLE);

        if (SDL_NumJoysticks() > 0) {
            game_joystick = SDL_JoystickOpen(0);

            if (!game_joystick) {
                fprintf(stderr, "No se pudo abrir el control: %s\n",
                        SDL_GetError());
            }
        }
    }

    const int exit_button = 8;
    const Uint32 hold_time = 2000;

    Uint32 press_start = 0;
    int button_held = 0;
    int exit_requested = 0;
    int return_to_menu = 0;
    int import_screen = 0;
    int import_finished = 0;
    int import_failed = 0;
    int import_handled = 0;

    int status = 0;

    while (1) {
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid)
            break;

        if (result < 0) {
            if (errno == EINTR)
                continue;

            perror("waitpid");
            break;
        }

        char import_status[32];

        if (!import_handled &&
            read_import_status(import_status, sizeof(import_status))) {

            if (!import_screen &&
                (strcmp(import_status, "STARTED") == 0 ||
                 strcmp(import_status, "DONE") == 0 ||
                 strcmp(import_status, "FAILED") == 0)) {
                printf("Importación USB detectada. Pausando emulador.\n");

                FILE *log = fopen("/home/pi/roms/usb-debug.log", "a");
                if (log) {
                    fprintf(log, "Intentando pausar PID: %d\n", pid);
                    fclose(log);
                }
                
                int stop_result = kill(pid, SIGSTOP);
                int stop_error = errno;

                log = fopen("/home/pi/roms/usb-debug.log", "a");
                if (log) {
                    fprintf(log,
                            "SIGSTOP: PID=%d, resultado=%d, errno=%d (%s)\n",
                            pid, stop_result, stop_error, strerror(stop_error));
                    fclose(log);
                }

                if (stop_result == 0) {
                    import_screen = 1;
                    import_finished = strcmp(import_status, "STARTED") != 0;
                    import_failed = strcmp(import_status, "FAILED") == 0;

                    if (game_joystick) {
                        SDL_JoystickClose(game_joystick);
                        game_joystick = NULL;
                    }

                    SDL_Quit();

                    if (init_sdl() == 0) {
                        open_joystick();
                    } else {
                        fprintf(stderr,
                                "No se pudo abrir la interfaz de importación.\n");
                        kill(pid, SIGCONT);
                        import_screen = 0;
                        import_handled = 1;

                        if (SDL_Init(SDL_INIT_JOYSTICK) == 0) {
                            SDL_JoystickEventState(SDL_ENABLE);
                            if (SDL_NumJoysticks() > 0)
                                game_joystick = SDL_JoystickOpen(0);
                        }
                    }
                } else {
                    FILE *log = fopen("/home/pi/roms/usb-debug.log", "a");
                    if (log) {
                        fprintf(log, "ERROR al pausar PID: %d\n", pid);
                        fclose(log);
                    }
                    
                    perror("No se pudo pausar el emulador");
                    import_handled = 1;
                }
            }

            if (import_screen && strcmp(import_status, "DONE") == 0) {
                import_finished = 1;
                import_failed = 0;
            } else if (import_screen &&
                       strcmp(import_status, "FAILED") == 0) {
                import_finished = 1;
                import_failed = 1;
            }
        }

        if (import_screen) {
            SDL_Event event;
            int choice = 0;

            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_JOYBUTTONDOWN) {
                    if (event.jbutton.button == 0 && import_finished)
                        choice = 1;
                    else if (event.jbutton.button == 1 && import_finished)
                        choice = 2;
                }

                if (event.type == SDL_JOYDEVICEADDED)
                    open_joystick();

                if (event.type == SDL_JOYDEVICEREMOVED &&
                    joystick &&
                    SDL_JoystickInstanceID(joystick) == event.jdevice.which) {
                    SDL_JoystickClose(joystick);
                    joystick = NULL;
                    open_joystick();
                }
            }

            if (joystick)
                SDL_JoystickUpdate();

            draw_import_screen(import_finished, import_failed);

            if (choice == 1) {
                printf("Reanudando el juego.\n");

                close_sdl();

                if (SDL_Init(SDL_INIT_JOYSTICK) == 0) {
                    SDL_JoystickEventState(SDL_ENABLE);

                    if (SDL_NumJoysticks() > 0)
                        game_joystick = SDL_JoystickOpen(0);
                }

                kill(pid, SIGCONT);
                unlink(IMPORT_STATUS_FILE);
                unlink(IMPORT_RESULTS_FILE);

                import_screen = 0;
                import_finished = 0;
                import_failed = 0;
                import_handled = 0;
                press_start = 0;
                button_held = 0;
            } else if (choice == 2) {
                printf("Cerrando juego y regresando al menú.\n");

                kill(pid, SIGTERM);
                kill(pid, SIGCONT);
                exit_requested = 1;
                return_to_menu = 1;
                break;
            }

            SDL_Delay(16);
            continue;
        }

        if (game_joystick &&
            SDL_JoystickNumButtons(game_joystick) > exit_button) {

            SDL_JoystickUpdate();

            if (SDL_JoystickGetButton(game_joystick, exit_button)) {
                Uint32 now = SDL_GetTicks();

                if (!button_held) {
                    press_start = now;
                    button_held = 1;
                } else if ((Uint32)(now - press_start) >= hold_time) {
                    printf("Cerrando juego...\n");

                    if (kill(pid, SIGTERM) == 0)
                        exit_requested = 1;

                    break;
                }
            } else {
                button_held = 0;
            }
        }

        SDL_Delay(10);
    }

    if (exit_requested || (waitpid(pid, &status, WNOHANG) == 0)) {
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                perror("waitpid");
                break;
            }
        }
    }

    if (game_joystick)
        SDL_JoystickClose(game_joystick);

    if (import_screen)
        close_sdl();
    else
        SDL_Quit();

    if (init_sdl() < 0)
        return -1;

    open_joystick();

    return return_to_menu ? 1 : 0;
}

static void draw_library(int selected, int scroll, int system)
{
    char title[64];
    char empty_message[64];

    snprintf(title, sizeof(title), "BIBLIOTECA %s",
             system_names[system]);

    snprintf(empty_message, sizeof(empty_message),
             "NO HAY JUEGOS %s", system_names[system]);

    draw_centered(renderer, 55, title,
                  (SDL_Color){255, 255, 255, 255}, 5);

    if (rom_count == 0) {
        draw_centered(renderer, 300, empty_message,
                      (SDL_Color){180, 195, 215, 255}, 4);

        draw_centered(renderer, 380, "COPIA ROMS A LA CARPETA",
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

        char number[16];
        snprintf(number, sizeof(number), "%d.", index + 1);

        draw_text(renderer, 150, 173 + i * 48,
                  number,
                  (SDL_Color){160, 175, 195, 255}, 2);

        draw_text(renderer, 190, 173 + i * 48,
                  roms[index],
                  (SDL_Color){255, 255, 255, 255}, 2);
    }

    draw_centered(renderer, 610, "CRUCETA MOVER",
                  (SDL_Color){160, 175, 195, 255}, 2);

    draw_centered(renderer, 650, "A ELEGIR   B REGRESAR",
                  (SDL_Color){160, 175, 195, 255}, 2);
}

int main(void)
{
    FILE *startup_log = fopen("/tmp/consola-startup.log", "w");

    if (startup_log) {
        fprintf(startup_log, "main() iniciado.\n");
        fclose(startup_log);
    }

    const char *systems[] = {"NES", "SNES", "GBA"};

    int running = 1;
    int selected = 0;
    int screen = 0;
    int library_selected = 0;
    int library_scroll = 0;

    Uint32 last_navigation = 0;
    Uint32 last_rom_scan = 0;

    /*
     * Cerrar psplash antes de que SDL tome el control
     * de la salida de video.
     */
    stop_psplash();

    if (init_sdl() < 0)
        return 1;

    open_joystick();

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

            if (event.type == SDL_JOYDEVICEADDED) {
                printf("Se conectó un control.\n");
                open_joystick();
            }

            if (event.type == SDL_JOYDEVICEREMOVED) {
                if (joystick &&
                    SDL_JoystickInstanceID(joystick) == event.jdevice.which) {

                    printf("Se desconectó el control.\n");

                    SDL_JoystickClose(joystick);
                    joystick = NULL;

                    open_joystick();
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

        if (action) {
            if (screen == 0) {
                scan_roms(selected);
                library_selected = 0;
                library_scroll = 0;
                last_rom_scan = now;
                screen = 1;
            } else if (screen == 1 && rom_count > 0) {
                char *game = strdup(roms[library_selected]);

                if (game) {
                    int game_result = launch_game(game, selected);

                    if (game_result < 0) {
                        free(game);
                        running = 0;
                        continue;
                    }

                    free(game);

                    if (game_result == 1) {
                        screen = 0;
                        library_selected = 0;
                        library_scroll = 0;
                        continue;
                    }

                    scan_roms(selected);

                    if (rom_count == 0) {
                        library_selected = 0;
                        library_scroll = 0;
                    } else if (library_selected >= rom_count) {
                        library_selected = rom_count - 1;
                    }
                }
            }
        }

        if (back && screen != 0)
            screen = 0;

        if (screen == 1 && now - last_rom_scan >= 1000) {
            scan_roms(selected);

            if (rom_count == 0) {
                library_selected = 0;
                library_scroll = 0;
            } else {
                if (library_selected >= rom_count)
                    library_selected = rom_count - 1;

                if (library_selected < library_scroll)
                    library_scroll = library_selected;

                if (library_selected >= library_scroll + VISIBLE_ROMS)
                    library_scroll = library_selected - VISIBLE_ROMS + 1;
            }

            last_rom_scan = now;
        }

        SDL_SetRenderDrawColor(renderer, 17, 22, 32, 255);
        SDL_RenderClear(renderer);

        if (screen == 0) {
            draw_centered(renderer, 105, "SELECCIONA SISTEMA",
                          (SDL_Color){255, 255, 255, 255}, 5);

            draw_card(renderer, 110, selected == 0, systems[0]);
            draw_card(renderer, 500, selected == 1, systems[1]);
            draw_card(renderer, 890, selected == 2, systems[2]);

            draw_centered(renderer, 565, "CRUCETA MOVER",
                          (SDL_Color){160, 175, 195, 255}, 2);

            draw_centered(renderer, 610, "A ELEGIR",
                          (SDL_Color){160, 175, 195, 255}, 2);

        } else if (screen == 1) {
            draw_library(library_selected, library_scroll, selected);

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
