#include <SDL2/SDL.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_video.h>
#include <SDL_scancode.h>
#include <chrono>
#include <stdio.h>
#include <string.h>

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 800;

const int GLOB_SCALE = 8;
const int GLOB_FONTSIZE = 32;

const int KEY_COUNT = 4;

const int BUTTON_WIDTH = 89; 
const int BUTTON_HEIGHT = 89;

SDL_Window *window = NULL;
SDL_Surface *screenSurface = NULL;
SDL_Renderer *renderer = NULL;
SDL_Rect screenRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

TTF_Font *gFont = NULL;
SDL_Rect statusBarBG;

typedef enum Inputs { UP, DOWN, LEFT, RIGHT, PAUSE, EXIT, TOTAL_INPUTS } Inputs;
bool KEYS[TOTAL_INPUTS];

auto lastUpdateTime = std::chrono::high_resolution_clock::now();

float targetFps = 120;
float dt = 0;

int charPosX = 0;
int charPosY = 0;
int charSpeed = 5;
float charFootstepTimer = 0;

// music, sfx
Mix_Music *music = NULL;
Mix_Chunk *step = NULL;

typedef enum LButtonState {
  BUTTON_STATE_YELLOW,
  BUTTON_STATE_RED,
  BUTTON_STATE_GREEN
} LButtonState;

class LTexture {
public:
  LTexture() {
    texture = NULL;
    width = 0;
    height = 0;
    scale = 0;
  }

  ~LTexture() { Free(); }

// anything inside #if will be ignored by compiler if SDL_TTF ... macro is not
// defined
#if defined(SDL_TTF_MAJOR_VERSION)

  bool LoadFromRenderedText(const char *text, SDL_Color tColor) {
    // free old texture
    Free();

    // render text
    SDL_Surface *tSurf = TTF_RenderText_Solid(gFont, text, tColor);
    if (tSurf == NULL) {
      printf("Unable to render text: %s\n", SDL_GetError());
      return false;
    }

    // create texture
    texture = SDL_CreateTextureFromSurface(renderer, tSurf);
    if (tSurf == NULL) {
      printf("Unable to make texture from text: %s\n", SDL_GetError());
      return false;
    }

    // because we're already giving fontsize, it's unnecessary to set scale too
    SetScale(1);

    width = tSurf->w;
    height = tSurf->h;

    SDL_FreeSurface(tSurf);

    return true;
  }

#endif

  bool LoadFromFile(const char *path) {
    // remove pre-existing texture
    Free();

    // load new one
    SDL_Surface *lSurf = IMG_Load(path);

    if (lSurf == NULL) {
      printf("Unable to load image: %s\n", SDL_GetError());
      return false;
    }

    // set color key to black
    SDL_SetColorKey(lSurf, SDL_TRUE, SDL_MapRGB(lSurf->format, 0, 0, 0));

    // make texture w/color key
    SDL_Texture *nTexture = SDL_CreateTextureFromSurface(renderer, lSurf);

    if (nTexture == NULL) {
      printf("Could not create texture: %s\n", SDL_GetError());
      return false;
    }

    width = lSurf->w;
    height = lSurf->h;

    // get rid of interim surface
    SDL_FreeSurface(lSurf);

    // set new texture
    texture = nTexture;

    // set scale to global
    SetScale(GLOB_SCALE);

    return true;
  }

  void Free() {
    if (texture != NULL) {
      SDL_DestroyTexture(texture);
      texture = NULL;
      width = 0;
      height = 0;
    }
  }

  void SetBlendMode(SDL_BlendMode blendMode) {
    // set blend mode
    SDL_SetTextureBlendMode(texture, blendMode);
  }

  // modulation ~ multiplication!

  void ModColor(Uint8 r, Uint8 g, Uint8 b) {
    SDL_SetTextureColorMod(texture, r, g, b);
  }

  void ModAlpha(Uint8 a) { SDL_SetTextureAlphaMod(texture, a); }

  void Render(int x, int y, SDL_Rect *clip = NULL) {
    // set render space on screen
    // sprite will be stretched to match
    SDL_Rect renderDest = {x, y, width * scale, height * scale};

    // give dest rect the dimensions of the src rect
    if (clip != NULL) {
      renderDest.w = clip->w * scale;
      renderDest.h = clip->h * scale;
    }

    // render to screen
    // pass in clip as src rect
    SDL_RenderCopy(renderer, texture, clip, &renderDest);
  }

  void RenderRotated(int x, int y, SDL_Rect *clip, double angle,
                     SDL_Point *center, SDL_RendererFlip flip) {
    SDL_Rect renderDest = {x, y, width * scale, height * scale};

    // careful! this isn't given a value by default
    if (clip != NULL) {
      renderDest.w = clip->w * scale;
      renderDest.h = clip->h * scale;
    }

    // pass sprite through rotation
    SDL_RenderCopyEx(renderer, texture, clip, &renderDest, angle, center, flip);
  }

  void RenderFill() {
    // stretch to fill screen
    SDL_Rect renderDest = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

    SDL_RenderCopy(renderer, texture, NULL, &renderDest);
  }

  void RenderIgnoreScale(int x, int y, int w, int h, SDL_Rect *clip = NULL) {
    // renders ignoring set scale; useful for ui that needs specific dimensions
    SDL_Rect renderDest = {x, y, w, h};

    // not sure what happens if clip wh don't match given wh; does it stretch?
    // yup, it does stretch!
    SDL_RenderCopy(renderer, texture, clip, &renderDest);
  }

  int GetWidth() { return width; }

  int GetHeight() { return height; }

  void SetScale(int nScale) { scale = nScale; }

private:
  SDL_Texture *texture;

  int width;
  int height;
  int scale;
};

LTexture tBackground;
LTexture tSampleText;
LTexture tSpriteSheet;
LTexture tButton;

class LSprite {
public:
  LSprite(LTexture *spriteSheet, SDL_Rect *spriteClips, int nFrames) {
    this->spriteSheet = spriteSheet;
    this->spriteClips = spriteClips;

    this->nFrames = nFrames;

    currentFrame = 0;
    fTimer = 0;
    fps = 4;
  }

  float GetFrameTimer() { return this->fTimer; }

  int GetFPS() { return this->fps; }

  void SetFPS(int fps) {
    if (fps < 0) {
      printf("Could not set FPS! Out of bounds.\n");
      return;
    }

    this->fps = fps;
  }

  void SetFrame(int f) {
    if (f < 0 || f >= nFrames) {
      printf("Could not set frame! Out of bounds.\n");
      return;
    }

    this->currentFrame = f;
  }

  bool Render(int x, int y) {
    bool movedFrame = false;

    if (fps > 0) {
      fTimer += dt;

      if (fTimer > (1.0 / fps)) {
        movedFrame = true;

        if (currentFrame + 1 == nFrames) {
          currentFrame = 0;
        } else {
          currentFrame++;
        }

        fTimer = 0;
      }
    }

    // draw
    spriteSheet->Render(x, y, &spriteClips[currentFrame]);

    return movedFrame;
  }

private:
  LTexture *spriteSheet;
  SDL_Rect *spriteClips;

  int currentFrame;
  int nFrames;
  int fps;
  float fTimer;
};

SDL_Rect charSpriteClips[] = {{0, 0, 16, 16}, {0, 16, 16, 16}};

LSprite characterSprite(&tSpriteSheet, charSpriteClips, 2);

SDL_Rect buttonSpriteClips[] = {{0, 0, 8, 8}, {0, 8, 8, 8}, {0, 16, 8, 8}};

class LButton {
public:
  LButton() {
    mPosition.x = 0;
    mPosition.y = 0;
    mState = BUTTON_STATE_RED;
  }

  void SetPosition(int x, int y) {
    mPosition.x = x;
    mPosition.y = y;
  }

  void HandleEvent(SDL_Event *e) {
    // enter this if any mouse events happen
    if (e->type == SDL_MOUSEMOTION || e->type == SDL_MOUSEBUTTONDOWN ||
        e->type == SDL_MOUSEBUTTONUP) {
      // get mouse pos
      int x, y;
      SDL_GetMouseState(&x, &y);

      // check if mouse within button
      bool inside = true;

      if (x < mPosition.x) {
        inside = false;
      }

      else if (x > mPosition.x + BUTTON_WIDTH) {
        inside = false;
      }

      else if (y < mPosition.y) {
        inside = false;
      }

      else if (y > mPosition.y + BUTTON_HEIGHT) {
        inside = false;
      }

      // change button state depending on mouse event
      if (!inside) {
        mState = BUTTON_STATE_RED;
      }

      else {
        switch (e->type) {
        // case SDL_MOUSEMOTION:
        //   mState = BUTTON_STATE_YELLOW;
        // break;
        case SDL_MOUSEBUTTONDOWN:
          mState = BUTTON_STATE_GREEN;
          break;
        case SDL_MOUSEBUTTONUP:
          mState = BUTTON_STATE_RED;
          break;
        }
      }
    }
  }

  void Render() {
    // render button texture
    // don't use sprite class because state is managed by instance
    tButton.RenderIgnoreScale(mPosition.x, mPosition.y, BUTTON_WIDTH,
                              BUTTON_HEIGHT, &buttonSpriteClips[mState]);
  }

private:
  SDL_Point mPosition;
  LButtonState mState;
};

LButton sampleButton;

bool Init() {
  // start sdl
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL init failed: %s\n", SDL_GetError());
    return false;
  }

  // make window
  window =
      SDL_CreateWindow("Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
  if (window == NULL) {
    printf("Window creation failed: %s\n", SDL_GetError());
    return false;
  }

  // make renderer for window
  renderer = SDL_CreateRenderer(
      window, -1,
      SDL_RENDERER_ACCELERATED); // we can | SDL_RENDERER_PRESENTVSYNC to enable
                                 // that, but don't; already have target fps
                                 // system

  if (renderer == NULL) {
    printf("Could not create renderer :%s\n", SDL_GetError());
    return false;
  }

  // adjust renderer color used
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);

  // start up sdl image loader
  int imageFlags = IMG_INIT_PNG; // this bitmask should result in a 1
  int initResult = IMG_Init(imageFlags) & imageFlags;
  if (!initResult) {
    printf("Could not load SDL image: %s\n", SDL_GetError());
    return false;
  }

  // start ttf loader
  if (TTF_Init() == -1) {
    printf("Could not load SDL ttf: %s\n", SDL_GetError());
    return false;
  }

  // start mixer
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
    printf("Could not load mixer: %s\n", SDL_GetError());
    return false;
  }

  // get window surface
  screenSurface = SDL_GetWindowSurface(window);

  // set keys
  for (int i = 0; i < KEY_COUNT; ++i)
    KEYS[i] = false;

  // position button
  sampleButton.SetPosition((SCREEN_WIDTH / 2) - (BUTTON_WIDTH / 2),
                           (SCREEN_HEIGHT / 2) - (BUTTON_HEIGHT / 2));

  return true;
}

bool LoadMedia() {
  bool success = true;

  // font
  gFont = TTF_OpenFont("../assets/pixel-font.ttf", GLOB_FONTSIZE);
  if (gFont == NULL) {
    printf("Failed to load gFont: %s\n", SDL_GetError());
    success = false;
  }

  // text
  tSampleText.LoadFromRenderedText("Click the button!", {255, 255, 255});

  // mk statusbar bg from text surf size
  statusBarBG = {0, SCREEN_HEIGHT - tSampleText.GetHeight() - 10, SCREEN_WIDTH,
                 tSampleText.GetHeight() + 10};

  // bg
  if (!tBackground.LoadFromFile("../assets/grass.png")) {
    printf("Could not load image: %s\n", SDL_GetError());
    success = false;
  }

  // char sprite
  if (!tSpriteSheet.LoadFromFile("../assets/ness.png")) {
    printf("Could not load image: %s\n", SDL_GetError());
    success = false;
  }

  // button sprite
  if (!tButton.LoadFromFile("../assets/button.png")) {
    printf("Could not load image: %s\n", SDL_GetError());
    success = false;
  }

  // sounds
  step = Mix_LoadWAV("../assets/step.wav");
  if (step == NULL) {
    printf("Failed to load SFX: %s\n", SDL_GetError());
    success = false;
  }

  // music
  music = Mix_LoadMUS("../assets/music.mp3");
  if (music == NULL) {
    printf("Failed to load music: %s\n", SDL_GetError());
    success = false;
  }

  return success;
}

void Close() {
  // free loaded images
  tSpriteSheet.Free();
  tBackground.Free();
  tSpriteSheet.Free();
  tButton.Free();

  // free sfx
  Mix_FreeChunk(step);

  // free music
  Mix_FreeMusic(music);

  // free window, renderer mem
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  renderer = NULL;
  window = NULL;

  // terminate sdl subsystems
  IMG_Quit();
  Mix_Quit();
  SDL_Quit();
}

int main(int argc, char *argv[]) {
  if (!Init())
    return 1;

  if (!LoadMedia())
    return 1;

  // window loop
  SDL_Event e;
  bool quit = false;
  while (!quit) {
    // get curr.time for dt calculation
    auto currentTime = std::chrono::high_resolution_clock::now();

    // compute dt
    dt = std::chrono::duration<float, std::chrono::seconds::period>(
             currentTime - lastUpdateTime)
             .count();

    // if dt does not exceed frame duration, move on
    if (dt < 1 / targetFps)
      continue;

    // poll returns 0 when no events, only run loop if events in it
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }

      // mouse event
      sampleButton.HandleEvent(&e);

      // pointer to array containing kb state
      // upd'd everytime PollEvent called, so we should put this in ev. loop
      const Uint8 *currentKeyStates = SDL_GetKeyboardState(NULL);

      // update input state using latter
      KEYS[UP] = currentKeyStates[SDL_SCANCODE_UP];
      KEYS[DOWN] = currentKeyStates[SDL_SCANCODE_DOWN];
      KEYS[LEFT] = currentKeyStates[SDL_SCANCODE_LEFT];
      KEYS[RIGHT] = currentKeyStates[SDL_SCANCODE_RIGHT];
      KEYS[PAUSE] = currentKeyStates[SDL_SCANCODE_P];
      KEYS[EXIT] = currentKeyStates[SDL_SCANCODE_ESCAPE];
    }

    // clear screen
    SDL_RenderClear(renderer);

    // esc check
    if (KEYS[EXIT]) {
      quit = true;
    }

    // music control, is broken :/
    // if (KEYS[PAUSE]) {
    //   if (Mix_PausedMusic()) {
    //     Mix_ResumeMusic();
    //   } else {
    //     Mix_PauseMusic();
    //   }
    // }

    // render bg
    tBackground.RenderFill();

    // button
    sampleButton.Render();

    // update player
    bool charMoving = KEYS[UP] || KEYS[DOWN] || KEYS[LEFT] || KEYS[RIGHT];
    if (charMoving) {
      // unpause
      characterSprite.SetFPS(4);

      // movement
      if (KEYS[UP])
        charPosY -= charSpeed;

      if (KEYS[DOWN])
        charPosY += charSpeed;

      if (KEYS[LEFT])
        charPosX -= charSpeed;

      if (KEYS[RIGHT])
        charPosX += charSpeed;
    }

    else {
      characterSprite.SetFPS(0);
    }

    // render player, play footsteps if moving frames
    // TODO: make this check not use the render function!
    if (characterSprite.Render(charPosX, charPosY) && charMoving) {
      Mix_PlayChannel(-1, step, 0);
    }

    // text with background
    SDL_RenderFillRect(renderer, &statusBarBG);
    tSampleText.Render((statusBarBG.w / 2) - (tSampleText.GetWidth() / 2),
                       statusBarBG.y +
                           (statusBarBG.h - tSampleText.GetHeight()) / 2);

    // play music on first free channel (-1)
    if (Mix_PlayingMusic() == 0) {
      Mix_PlayMusic(music, -1);
    }

    // update screen
    SDL_RenderPresent(renderer);

    // update last time
    lastUpdateTime = currentTime;
  }

  return 1;
}
