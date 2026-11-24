#include <stdio.h>
#include <SDL.h>

int main(void)
{
    SDL_Joystick *joystick = NULL;
    SDL_Event event;

    if (SDL_Init(SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Error SDL: %s\n", SDL_GetError());
        return 1;
    }

    printf("Joysticks detectados: %d\n", SDL_NumJoysticks());

    if (SDL_NumJoysticks() == 0) {
        printf("No se detectaron controles.\n");
        SDL_Quit();
        return 1;
    }

    joystick = SDL_JoystickOpen(0);

    if (joystick == NULL) {
        fprintf(stderr, "Error al abrir joystick: %s\n",
                SDL_GetError());
        SDL_Quit();
        return 1;
    }

    printf("Control: %s\n", SDL_JoystickName(joystick));
    char guidString[33];
    SDL_JoystickGetGUIDString(
        SDL_JoystickGetGUID(joystick),
        guidString,
        sizeof(guidString)
    );
    printf("GUID: %s\n", guidString);
    printf("Botones: %d\n", SDL_JoystickNumButtons(joystick));
    printf("Ejes: %d\n", SDL_JoystickNumAxes(joystick));
    printf("Presiona botones o mueve las palancas.\n");
    printf("Presiona Ctrl+C para terminar.\n");

    SDL_JoystickEventState(SDL_ENABLE);

    while (1) {
        while (SDL_PollEvent(&event)) {

            switch (event.type) {

                case SDL_JOYBUTTONDOWN:
                    printf("Boton presionado: %d\n",
                           event.jbutton.button);
                    break;

                case SDL_JOYBUTTONUP:
                    printf("Boton liberado: %d\n",
                           event.jbutton.button);
                    break;

                case SDL_JOYAXISMOTION:
                    printf("Eje %d: %d\n",
                           event.jaxis.axis,
                           event.jaxis.value);
                    break;

                case SDL_JOYHATMOTION:
                    printf("D-Pad: %d\n",
                           event.jhat.value);
                    break;
            }
        }

        SDL_Delay(10);
    }

    SDL_JoystickClose(joystick);
    SDL_Quit();

    return 0;
}
