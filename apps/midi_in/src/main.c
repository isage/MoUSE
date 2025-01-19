#include <SDL.h>
#include <SDL_image.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/power.h>
#include <psp2/shellutil.h>
#include <psp2/vshbridge.h>
#include <taihen.h>

#include <libmouse.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

#define printf sceClibPrintf

unsigned int _newlib_heap_size_user = 220 * 1024 * 1024;

static SDL_Window *g_window        = NULL;
static SDL_Renderer *g_renderer    = NULL;
static SDL_Texture *g_tex_inactive = NULL;
static SDL_Texture *g_tex_active   = NULL;
static SDL_Texture *g_tex_connect   = NULL;
static SDL_Texture *g_tex_particle_star = NULL;
static SDL_Texture *g_tex_particle_spot = NULL;
static SDL_Thread *g_thread;
static uint32_t g_last_tick = 0;

SDL_mutex* g_mutex;
tsf* g_tsf;

static int g_mode                  = 1;
static int g_preset                = 0;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t type;
    float radius;
    float angle;
    int32_t lifetime;
    uint8_t alive;

    uint8_t note;
} particle;

particle particles[256] = {0};

static Uint64 SDL_rand_state;
static uint8_t SDL_rand_initialized = 0;

enum MidiMessageType
{
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_KEY_PRESSURE = 0xA0,
    MIDI_CONTROL_CHANGE = 0xB0,
    MIDI_PROGRAM_CHANGE = 0xC0,
    MIDI_CHANNEL_PRESSURE = 0xD0,
    MIDI_PITCH_BEND = 0xE0,
    MIDI_SET_TEMPO = 0x51
};

void SDL_srand(Uint64 seed)
{
    if (!seed) {
        seed = SDL_GetPerformanceCounter();
    }
    SDL_rand_state = seed;
    SDL_rand_initialized = 1;
}

Uint32 SDL_rand_bits_r(Uint64 *state)
{
    if (!state) {
        return 0;
    }

    // The C and A parameters of this LCG have been chosen based on hundreds
    // of core-hours of testing with PractRand and TestU01's Crush.
    // Using a 32-bit A improves performance on 32-bit architectures.
    // C can be any odd number, but < 256 generates smaller code on ARM32
    // These values perform as well as a full 64-bit implementation against
    // Crush and PractRand. Plus, their worst-case performance is better
    // than common 64-bit constants when tested against PractRand using seeds
    // with only a single bit set.

    // We tested all 32-bit and 33-bit A with all C < 256 from a v2 of:
    // Steele GL, Vigna S. Computationally easy, spectrally good multipliers
    // for congruential pseudorandom number generators.
    // Softw Pract Exper. 2022;52(2):443-458. doi: 10.1002/spe.3030
    // https://arxiv.org/abs/2001.05304v2

    *state = *state * 0xff1cd035ul + 0x05;

    // Only return top 32 bits because they have a longer period
    return (Uint32)(*state >> 32);
}

Sint32 SDL_rand_r(Uint64 *state, Sint32 n)
{
    // Algorithm: get 32 bits from SDL_rand_bits() and treat it as a 0.32 bit
    // fixed point number. Multiply by the 31.0 bit n to get a 31.32 bit
    // result. Shift right by 32 to get the 31 bit integer that we want.

    if (n < 0) {
        // The algorithm looks like it works for numbers < 0 but it has an
        // infinitesimal chance of returning a value out of range.
        // Returning -SDL_rand(abs(n)) blows up at INT_MIN instead.
        // It's easier to just say no.
        return 0;
    }

    // On 32-bit arch, the compiler will optimize to a single 32-bit multiply
    Uint64 val = (Uint64)SDL_rand_bits_r(state) * n;
    return (Sint32)(val >> 32);
}

float SDL_randf_r(Uint64 *state)
{
    // Note: its using 24 bits because float has 23 bits significand + 1 implicit bit
    return (SDL_rand_bits_r(state) >> (32 - 24)) * 0x1p-24f;
}


Sint32 SDL_rand(Sint32 n)
{
    if (!SDL_rand_initialized) {
        SDL_srand(0);
    }

    return SDL_rand_r(&SDL_rand_state, n);
}

float SDL_randf(void)
{
    if (!SDL_rand_initialized) {
        SDL_srand(0);
    }

    return SDL_randf_r(&SDL_rand_state);
}

static void AudioCallback(void* data, Uint8 *stream, int len)
{
    // Render the audio samples in float format
    int SampleCount = (len / (2 * sizeof(uint16_t))); //2 output channels
    SDL_LockMutex(g_mutex); //get exclusive lock
    tsf_render_short(g_tsf, (uint16_t*)stream, SampleCount, 0);
    SDL_UnlockMutex(g_mutex);
}

int init()
{
  SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

  if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    return -1;

  if ((g_window
       = SDL_CreateWindow("MoUSE", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 960, 544, SDL_WINDOW_SHOWN))
      == NULL)
    return -1;

  if ((g_renderer = SDL_CreateRenderer(g_window, -1, 0)) == NULL)
    return -1;

  SDL_RendererInfo info;
  SDL_GetRendererInfo(g_renderer, &info);
  sceClibPrintf("using: %s renderer\n", info.name);

  SDL_version ver;
  SDL_GetVersion(&ver);
  sceClibPrintf("using: %d.%d.%d sdl\n", ver.major,ver.minor,ver.patch);

  int flags = IMG_INIT_PNG;
  int initted = IMG_Init(flags);
  if((initted & flags) != flags) {
    fprintf(stderr, "IMG_Init: Failed to init required png support: {}", IMG_GetError());
    return -1;
  }


  SDL_Surface *temp = SDL_LoadBMP("data/inactive.bmp");
  if (!temp)
    return -1;
  g_tex_inactive = SDL_CreateTextureFromSurface(g_renderer, temp);
  if (!g_tex_inactive)
    return -1;
  SDL_FreeSurface(temp);

  temp = SDL_LoadBMP("data/active.bmp");
  if (!temp)
    return -1;
  g_tex_active = SDL_CreateTextureFromSurface(g_renderer, temp);
  if (!g_tex_active)
    return -1;
  SDL_FreeSurface(temp);

  temp = IMG_Load("data/star.png");
  if (!temp)
    return -1;
  g_tex_particle_star = SDL_CreateTextureFromSurface(g_renderer, temp);
  if (!g_tex_particle_star)
    return -1;
  SDL_FreeSurface(temp);

  temp = IMG_Load("data/spot.png");
  if (!temp)
    return -1;
  g_tex_particle_spot = SDL_CreateTextureFromSurface(g_renderer, temp);
  if (!g_tex_particle_spot)
    return -1;
  SDL_FreeSurface(temp);

  temp = IMG_Load("data/connect.png");
  if (!temp)
    return -1;
  g_tex_connect = SDL_CreateTextureFromSurface(g_renderer, temp);
  if (!g_tex_connect)
    return -1;
  SDL_FreeSurface(temp);


  SDL_GameControllerOpen(0);

  SDL_srand(0);

  SDL_AudioSpec OutputAudioSpec;
  OutputAudioSpec.freq = 44100;
  OutputAudioSpec.format = AUDIO_S16;
  OutputAudioSpec.channels = 2;
  OutputAudioSpec.samples = 4096;
  OutputAudioSpec.callback = AudioCallback;

  g_tsf = tsf_load_filename("data/florestan-subset.sf2");
  if (!g_tsf)
  {
    fprintf(stderr, "Could not load SoundFont\n");
    return -1;
  }

  for (int i = 0; i < 32; i++)
  {
      tsf_channel_set_volume(g_tsf, i, 1.0f);
      tsf_channel_set_bank_preset(g_tsf, i, i, 0);
  }

  // Set the SoundFont rendering output mode
  tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, OutputAudioSpec.freq, 0.0f);

  g_mutex = SDL_CreateMutex();

  // Request the desired audio output format
  if (SDL_OpenAudio(&OutputAudioSpec, 0) < 0)
  {
    fprintf(stderr, "Could not open the audio hardware or the desired audio output format\n");
    return -11;
  }

  // Start the actual audio playback here
  // The audio thread will begin to call our AudioCallback function
  SDL_PauseAudio(0);

  return 0;
}

void start()
{
  g_mode = 1;
}

void stop()
{
  g_mode = 0;
}

int updateMidiInput(void *data)
{
    particle* pparticles = (particle*)data;
    while(1)
    {
      if (libmouse_usb_in_attached())
      {
        uint8_t reply[64] = {0};
        int res = libmouse_usb_read(reply, 64);
        if (res < 0)
        {
        }
        else
        {
            uint8_t cmd = reply[1] & 0xF0;
            uint8_t channel = reply[1] & 0x0F;

            switch(cmd)
            {
                case MIDI_NOTE_ON:
                    SDL_LockMutex(g_mutex);
                    tsf_channel_note_on(g_tsf, channel, reply[2] & 0x7F, (float)(reply[3] & 0x7F) / 127.0f);
                    SDL_UnlockMutex(g_mutex);
                    for(int i = 0; i < 256; i ++)
                    {
                        if (!pparticles[i].alive)
                        {

                            pparticles[i].alive = 1;
                            pparticles[i].angle = 0;
                            pparticles[i].x = SDL_rand(960);
                            pparticles[i].y = SDL_rand(544);
                            pparticles[i].radius = ((float)(reply[3] & 0x7F) / 127.0f) * 2.f;
                            pparticles[i].r = SDL_rand(255);
                            pparticles[i].g = SDL_rand(255);
                            pparticles[i].b = SDL_rand(255);
                            pparticles[i].type = SDL_rand(100) > 50;
                            pparticles[i].lifetime = 1500;

                            pparticles[i].note = reply[2] & 0x7F;
                            break;
                        }
                    }


                    break;
                case MIDI_NOTE_OFF:
                    SDL_LockMutex(g_mutex);
                    tsf_channel_note_off(g_tsf, channel, reply[2] & 0x7F);
                    SDL_UnlockMutex(g_mutex);
                    break;
                case MIDI_CONTROL_CHANGE:
                    SDL_LockMutex(g_mutex);
                    tsf_channel_midi_control(g_tsf, channel, reply[2] & 0x7F, reply[3] & 0x7F);
                    SDL_UnlockMutex(g_mutex);
                    break;
                case MIDI_PITCH_BEND:
                    SDL_LockMutex(g_mutex);
                    tsf_channel_set_pitchwheel(g_tsf, channel, (reply[3] & 0x7F) << 7 | (reply[2] & 0x7F));
                    SDL_UnlockMutex(g_mutex);
                    break;
                default:
                    break;
            }

        }

      }
    }
}

void pollInput()
{
  SDL_Event event;

  while (SDL_PollEvent(&event))
  {
    switch (event.type)
    {
      case SDL_CONTROLLERBUTTONDOWN:
        if (g_mode == 0)
        {
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            start();
        }
        else if (g_mode == 1)
        {
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B)
          {
            stop();
          }
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
          {
            g_preset--;
            if (g_preset < 0)
               g_preset = tsf_get_presetcount(g_tsf) - 1;

            tsf_channel_set_presetindex(g_tsf, 0, g_preset);
          }
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
          {
            g_preset++;
            if (g_preset >= tsf_get_presetcount(g_tsf))
               g_preset = 0;

            tsf_channel_set_presetindex(g_tsf, 0, g_preset);
          }
        }
        break;
      case SDL_CONTROLLERBUTTONUP:
        break;

      default:
        break;
    }
  }
}

void drawParticles()
{
    uint32_t cur_tick = SDL_GetTicks();
    uint32_t passed = cur_tick - g_last_tick;
    g_last_tick = cur_tick;

    for(int i = 0; i < 256; i ++)
    {
        if (particles[i].alive)
        {
            particles[i].lifetime -= passed;
            if (particles[i].lifetime <= 0)
            {
                particles[i].lifetime = 0;
                particles[i].alive = 0;
                particles[i].angle = 0;
            }
            else
            {
                particles[i].angle += passed / 5.f;
            }
        }

        if (particles[i].alive)
        {
            SDL_Texture *cur = NULL;
            if (particles[i].type == 0)
            {
                cur = g_tex_particle_spot;
            }
            else
            {
                cur = g_tex_particle_star;
            }

            SDL_SetTextureAlphaMod(cur, particles[i].lifetime / 10);
            SDL_SetTextureColorMod(cur, particles[i].r, particles[i].g, particles[i].b);

            SDL_Rect dst = {
                particles[i].x,
                particles[i].y,
                64.0f * particles[i].radius,
                64.0f * particles[i].radius
            };
            SDL_RenderCopyEx(g_renderer, cur, NULL, &dst, particles[i].angle, NULL, 0);
        }
    }
}

int main(int argc, char *argv[])
{

  if (init() < 0)
    return 0;

  int search_param[2];
  SceUID res = _vshKernelSearchModuleByName("libmouse", search_param);
  if (res <= 0)
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Missing kernel driver", "Please install libmouse.skprx", g_window);
    return -1;
  }

  // ignore errors
  //libmouse_udcd_stop();
  libmouse_usb_start();

  // because usb read is blocking we do it on separate thread
  SDL_CreateThread(updateMidiInput, "midi", particles);

  g_last_tick = SDL_GetTicks();

  while (1)
  {
    SDL_RenderClear(g_renderer);

    SDL_RenderCopy(g_renderer, g_mode ? g_tex_active : g_tex_inactive, NULL, NULL);

    pollInput();

    if (g_mode == 1)
    {
      if (!libmouse_usb_in_attached())
      {
        SDL_RenderCopy(g_renderer, g_tex_connect, NULL, NULL);
      }
      drawParticles();
    }
    SDL_RenderPresent(g_renderer);
    SDL_Delay(1); // yield
  }

  SDL_DestroyTexture(g_tex_inactive);
  SDL_DestroyTexture(g_tex_active);
  SDL_DestroyRenderer(g_renderer);
  SDL_DestroyWindow(g_window);

  SDL_Quit();
  return 0;
}
