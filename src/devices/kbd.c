#include "devices/kbd.h"
#include <ctype.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Keyboard data register port. */
#define DATA_REG 0x60

/* ============================================================
   TOP-HALF / BOTTOM-HALF CONFIGURATION

   Set to 1 to enable top/bottom half split (educational demo).
   Set to 0 for original behavior (all processing in interrupt).
   ============================================================ */
#define KBD_USE_BOTTOM_HALF 1

/* Set to 1 to see debug output showing the top/bottom half flow */
#define KBD_DEBUG_TRACE 0

#if KBD_USE_BOTTOM_HALF
/* Raw scancode buffer - written by top half, read by bottom half */
#define RAW_BUFFER_SIZE 64
static uint16_t raw_buffer[RAW_BUFFER_SIZE];
static volatile size_t raw_head; /* Written by top half */
static volatile size_t raw_tail; /* Read by bottom half */

/* Semaphore to wake the bottom half worker thread */
static struct semaphore kbd_sema;

/* Forward declaration of bottom half worker */
static void kbd_bottom_half(void* aux UNUSED);
#endif

/* Current state of shift keys.
   True if depressed, false otherwise. */
static bool left_shift, right_shift; /* Left and right Shift keys. */
static bool left_alt, right_alt;     /* Left and right Alt keys. */
static bool left_ctrl, right_ctrl;   /* Left and right Ctl keys. */

/* Status of Caps Lock.
   True when on, false when off. */
static bool caps_lock;

/* Number of keys pressed. */
static int64_t key_cnt;

static intr_handler_func keyboard_interrupt;

/* Initializes the keyboard. */
void kbd_init(void) {
#if KBD_USE_BOTTOM_HALF
  /* Initialize the raw buffer */
  raw_head = raw_tail = 0;

  /* Initialize semaphore (starts at 0 - bottom half will block) */
  sema_init(&kbd_sema, 0);

  /* Create the bottom half worker thread */
  thread_create("kbd-worker", PRI_DEFAULT, kbd_bottom_half, NULL);

  printf("Keyboard: using top-half/bottom-half split\n");
#endif

  intr_register_ext(0x21, keyboard_interrupt, "8042 Keyboard");
}

/* Prints keyboard statistics. */
void kbd_print_stats(void) { printf("Keyboard: %lld keys pressed\n", key_cnt); }

/* Maps a set of contiguous scancodes into characters. */
struct keymap {
  uint8_t first_scancode; /* First scancode. */
  const char* chars;      /* chars[0] has scancode first_scancode,
                                  chars[1] has scancode first_scancode + 1,
                                  and so on to the end of the string. */
};

/* Keys that produce the same characters regardless of whether
   the Shift keys are down.  Case of letters is an exception
   that we handle elsewhere.  */
static const struct keymap invariant_keymap[] = {
    {0x01, "\033"}, /* Escape. */
    {0x0e, "\b"},        {0x0f, "\tQWERTYUIOP"}, {0x1c, "\r"},
    {0x1e, "ASDFGHJKL"}, {0x2c, "ZXCVBNM"},      {0x37, "*"},
    {0x39, " "},         {0x53, "\177"}, /* Delete. */
    {0, NULL},
};

/* Characters for keys pressed without Shift, for those keys
   where it matters. */
static const struct keymap unshifted_keymap[] = {
    {0x02, "1234567890-="}, {0x1a, "[]"}, {0x27, ";'`"}, {0x2b, "\\"}, {0x33, ",./"}, {0, NULL},
};

/* Characters for keys pressed with Shift, for those keys where
   it matters. */
static const struct keymap shifted_keymap[] = {
    {0x02, "!@#$%^&*()_+"}, {0x1a, "{}"}, {0x27, ":\"~"}, {0x2b, "|"}, {0x33, "<>?"}, {0, NULL},
};

static bool map_key(const struct keymap[], unsigned scancode, uint8_t*);

/* Process a single scancode - shared logic used by both modes */
static void process_scancode(unsigned code) {
  /* Status of shift keys. */
  bool shift = left_shift || right_shift;
  bool alt = left_alt || right_alt;
  bool ctrl = left_ctrl || right_ctrl;

  /* False if key pressed, true if key released. */
  bool release;

  /* Character that corresponds to `code'. */
  uint8_t c;

  /* Bit 0x80 distinguishes key press from key release
     (even if there's a prefix). */
  release = (code & 0x80) != 0;
  code &= ~0x80u;

  /* Interpret key. */
  if (code == 0x3a) {
    /* Caps Lock. */
    if (!release)
      caps_lock = !caps_lock;
  } else if (map_key(invariant_keymap, code, &c) ||
             (!shift && map_key(unshifted_keymap, code, &c)) ||
             (shift && map_key(shifted_keymap, code, &c))) {
    /* Ordinary character. */
    if (!release) {
      /* Reboot if Ctrl+Alt+Del pressed. */
      if (c == 0177 && ctrl && alt)
        shutdown_reboot();

      /* Handle Ctrl, Shift.
             Note that Ctrl overrides Shift. */
      if (ctrl && c >= 0x40 && c < 0x60) {
        /* A is 0x41, Ctrl+A is 0x01, etc. */
        c -= 0x40;
      } else if (shift == caps_lock)
        c = tolower(c);

      /* Handle Alt by setting the high bit.
             This 0x80 is unrelated to the one used to
             distinguish key press from key release. */
      if (alt)
        c += 0x80;

      /* Append to keyboard buffer. */
      if (!input_full()) {
        key_cnt++;
        input_putc(c);
      }
    }
  } else {
    /* Maps a keycode into a shift state variable. */
    struct shift_key {
      unsigned scancode;
      bool* state_var;
    };

    /* Table of shift keys. */
    static const struct shift_key shift_keys[] = {
        {0x2a, &left_shift}, {0x36, &right_shift},  {0x38, &left_alt}, {0xe038, &right_alt},
        {0x1d, &left_ctrl},  {0xe01d, &right_ctrl}, {0, NULL},
    };

    const struct shift_key* key;

    /* Scan the table. */
    for (key = shift_keys; key->scancode != 0; key++)
      if (key->scancode == code) {
        *key->state_var = !release;
        break;
      }
  }
}

#if KBD_USE_BOTTOM_HALF
/* ============================================================
   TOP HALF - Interrupt Handler

   Runs with interrupts DISABLED.
   Goal: Do minimal work and return FAST.

   Only does:
   1. Read the raw scancode from hardware
   2. Put it in a buffer
   3. Wake the bottom half
   ============================================================ */
static void keyboard_interrupt(struct intr_frame* args UNUSED) {
  unsigned code;

  /* Read scancode from hardware - MUST do this to acknowledge interrupt */
  code = inb(DATA_REG);
  if (code == 0xe0)
    code = (code << 8) | inb(DATA_REG);

#if KBD_DEBUG_TRACE
  printf("[TOP HALF] IRQ! scancode=0x%04x, buffering...\n", code);
#endif

  /* Buffer the raw scancode for the bottom half to process */
  size_t next_head = (raw_head + 1) % RAW_BUFFER_SIZE;
  if (next_head != raw_tail) { /* Buffer not full */
    raw_buffer[raw_head] = code;
    raw_head = next_head;
  }
  /* If buffer full, we drop the scancode - can't block in interrupt! */

  /* Wake the bottom half worker thread */
  sema_up(&kbd_sema);
}

/* ============================================================
   BOTTOM HALF - Worker Thread

   Runs as a normal kernel thread with interrupts ENABLED.
   Can take as long as needed - won't block other interrupts.

   Does all the complex processing:
   - Scancode translation
   - Shift/Ctrl/Alt handling
   - Character mapping
   ============================================================ */
static void kbd_bottom_half(void* aux UNUSED) {
  for (;;) {
    /* Sleep until top half wakes us */
    sema_down(&kbd_sema);

#if KBD_DEBUG_TRACE
    printf("[BOTTOM HALF] Woke up! Processing buffered scancodes...\n");
#endif

    /* Process all buffered scancodes */
    while (raw_tail != raw_head) {
      unsigned code = raw_buffer[raw_tail];
      raw_tail = (raw_tail + 1) % RAW_BUFFER_SIZE;

#if KBD_DEBUG_TRACE
      printf("[BOTTOM HALF] Processing scancode 0x%04x\n", code);
#endif

      /* Do the actual scancode processing - this is the "heavy" work
         that we moved out of the interrupt handler */
      process_scancode(code);
    }
  }
}

#else
/* ============================================================
   ORIGINAL MODE - All processing in interrupt handler

   This is simpler but blocks interrupts longer.
   Fine for Pintos since the processing is fast anyway.
   ============================================================ */
static void keyboard_interrupt(struct intr_frame* args UNUSED) {
  unsigned code;

  /* Read scancode, including second byte if prefix code. */
  code = inb(DATA_REG);
  if (code == 0xe0)
    code = (code << 8) | inb(DATA_REG);

  /* Process immediately in interrupt context */
  process_scancode(code);
}
#endif

/* Scans the array of keymaps K for SCANCODE.
   If found, sets *C to the corresponding character and returns
   true.
   If not found, returns false and C is ignored. */
static bool map_key(const struct keymap k[], unsigned scancode, uint8_t* c) {
  for (; k->first_scancode != 0; k++)
    if (scancode >= k->first_scancode && scancode < k->first_scancode + strlen(k->chars)) {
      *c = k->chars[scancode - k->first_scancode];
      return true;
    }

  return false;
}
