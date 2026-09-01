/**
 * SFX - Non-blocking Sound Effects Implementation for Porkchop
 *
 * ==[ CHEF'S AUDIO ENGINE ]== 
 * - Note sequences: {freq, duration, pause} steps
 * - update() ticks without blocking
 * - Ring buffer for callback-safe event queuing
 * 
 * Adapted from Sirloin audio system.
 */

#include "sfx.h"
#include "../core/config.h"
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace SFX {

// ==[ SOUND DEFINITIONS ]== arrays of {freq, duration, pause}
// freq=0 means silence, duration=0 means END of sequence
//
// ==[ OPTION D: HYBRID PAPA PIG ]==
// Clean terminal sounds for frequent events, pig personality for celebrations.
// Professional but warm. The tool of a seasoned hacker pig.
//
struct Note {
    uint16_t freq;      // Hz (0 = silence)
    uint16_t duration;  // ms
    uint16_t pause;     // ms after this note
};

// CLICK: Soft mechanical switch tick
static const Note SND_CLICK[] = {
    {1050, 6, 0},
    {0, 0, 0}
};

// MENU_CLICK: Slightly lower, cushioned click
static const Note SND_MENU_CLICK[] = {
    {900, 7, 0},
    {0, 0, 0}
};

// TERMINAL_TICK: Mother-style hum pulse (deterministic round-robin)
static const Note SND_TERM_TICK_A[] = {
    {260, 12, 2},
    {540, 3, 0},
    {0, 0, 0}
};
static const Note SND_TERM_TICK_B[] = {
    {240, 13, 2},
    {500, 3, 0},
    {0, 0, 0}
};
static const Note SND_TERM_TICK_C[] = {
    {280, 11, 2},
    {600, 3, 0},
    {0, 0, 0}
};
static const Note SND_TERM_TICK_D[] = {
    {220, 14, 2},
    {460, 3, 2},
    {220, 10, 0},
    {0, 0, 0}
};
static const Note SND_TERM_TICK_E[] = {
    {300, 10, 2},
    {620, 3, 2},
    {250, 12, 0},
    {0, 0, 0}
};

// NETWORK_NEW: Short, quiet ping (fires often)
static const Note SND_NETWORK[] = {
    {820, 5, 0},
    {0, 0, 0}
};

// CLIENT_FOUND: Slightly brighter than network, still short
static const Note SND_CLIENT_FOUND[] = {
    {1000, 6, 0},
    {0, 0, 0}
};

// DEAUTH: TX pulse — soft descending pip (the pig pokes the airwaves)
// Short + warm interval (~perfect 4th) = pleasant at high repetition
static const Note SND_DEAUTH[] = {
    {700, 18, 0},     // bright onset
    {520, 14, 0},     // warm resolve (descending = energy going "out")
    {0, 0, 0}
};

// PMKID: "Truffle found" - pig's ears perk up, quick ascending pair
static const Note SND_PMKID[] = {
    {1000, 50, 15},
    {1300, 50, 0},
    {0, 0, 0}
};

// HANDSHAKE: "Got 'em" - complete phrase with warm resolution
// 800→1000→1200 then resolve back to 1000 (closure)
static const Note SND_HANDSHAKE[] = {
    {800, 60, 15},
    {1000, 60, 15},
    {1200, 80, 15},
    {1000, 100, 0},  // Resolve - the satisfying "done"
    {0, 0, 0}
};

// ACHIEVEMENT: "Papa proud" - warm, earned feeling
static const Note SND_ACHIEVEMENT[] = {
    {600, 80, 25},
    {900, 80, 25},
    {1200, 100, 0},
    {0, 0, 0}
};

// LEVEL_UP: "Oink of glory" - ascending major, proper celebration
static const Note SND_LEVEL_UP[] = {
    {500, 80, 20},
    {700, 80, 20},
    {1000, 80, 20},
    {1200, 120, 0},
    {0, 0, 0}
};

// JACKPOT_XP: Exciting but not annoying - quick rising phrase
static const Note SND_JACKPOT[] = {
    {700, 50, 15},
    {900, 50, 15},
    {1100, 50, 15},
    {1400, 100, 0},
    {0, 0, 0}
};

// ULTRA_STREAK: Big moment - extended celebration
static const Note SND_ULTRA_STREAK[] = {
    {500, 60, 15},
    {700, 60, 15},
    {900, 60, 15},
    {1100, 80, 20},
    {1400, 150, 0},
    {0, 0, 0}
};

// CALL_RING: Phone pip - attention getter
static const Note SND_RING[] = {
    {900, 80, 40},
    {1100, 80, 0},
    {0, 0, 0}
};

// SYNC_COMPLETE: Success - clean resolution
static const Note SND_SYNC_COMPLETE[] = {
    {800, 70, 20},
    {1000, 70, 20},
    {1200, 100, 0},
    {0, 0, 0}
};

// ERROR: Soft low double tap
static const Note SND_ERROR[] = {
    {330, 50, 20},
    {250, 60, 0},
    {0, 0, 0}
};

// BOOT: Nostromo-style long boot sequence (2-3s)
static const Note SND_BOOT[] = {
    {250, 650, 140},  // low hum pulse
    {600, 12, 30},
    {700, 12, 30},
    {520, 12, 60},
    {240, 180, 80},  // tape thud
    {800, 12, 30},
    {640, 12, 30},
    {500, 12, 60},
    {900, 10, 30},
    {700, 10, 30},
    {850, 10, 60},
    {280, 230, 70},  // tape thud
    {310, 320, 90},
    {360, 360, 0},
    {0, 0, 0}
};

// PIGSYNC_BOOT: Shorter wake sequence for FA/TH/ER
static const Note SND_PIGSYNC_BOOT[] = {
    {270, 480, 140},
    {540, 12, 40},
    {660, 12, 40},
    {560, 12, 80},
    {240, 160, 70},  // tape thud
    {820, 10, 40},
    {700, 10, 60},
    {310, 210, 70},
    {340, 220, 70},
    {280, 240, 0},
    {0, 0, 0}
};

// SIREN: Quick alternating for visual effect sync
static const Note SND_SIREN[] = {
    {500, 35, 0},
    {800, 35, 0},
    {500, 35, 0},
    {800, 35, 0},
    {0, 0, 0}
};

// ==[ SPECTRUM MODE SOUNDS ]==

// SIGNAL_LOST: "Gone" - sad descending
static const Note SND_SIGNAL_LOST[] = {
    {800, 80, 25},
    {500, 120, 0},
    {0, 0, 0}
};

// CHANNEL_LOCK: Quick confirmation tick
static const Note SND_CHANNEL_LOCK[] = {
    {900, 40, 0},
    {0, 0, 0}
};

// REVEAL_START: Ascending pair - "searching"
static const Note SND_REVEAL_START[] = {
    {700, 40, 15},
    {1000, 50, 0},
    {0, 0, 0}
};

// ==[ CHALLENGE SOUNDS ]==

// CHALLENGE_COMPLETE: "Nice work" - similar to achievement, lighter
static const Note SND_CHALLENGE_COMPLETE[] = {
    {700, 60, 20},
    {900, 60, 20},
    {1100, 80, 0},
    {0, 0, 0}
};

// CHALLENGE_SWEEP: "Legendary" - the big one with resolve
static const Note SND_CHALLENGE_SWEEP[] = {
    {800, 70, 20},
    {1000, 70, 20},
    {1200, 70, 20},
    {1500, 100, 15},
    {1200, 80, 0},  // Resolve down - closure
    {0, 0, 0}
};

// YOU_DIED: "Dark Souls" style death sound
// Impact (43Hz), then F3 wobble (172/178), with dissonant B3/Eb4, fading to sub
static const Note SND_YOU_DIED[] = {
    {220, 200, 20},  // Impact (was 43Hz sub-bass, raised to audible)
    {344, 80, 0},    // F4 wobble 1
    {356, 80, 0},    // F4 wobble 1
    {344, 80, 0},    // F4 wobble 2
    {356, 80, 0},    // F4 wobble 2
    {494, 60, 0},    // B4 (poison/dissonance)
    {344, 80, 0},    // F4 wobble 3
    {356, 80, 0},    // F4 wobble 3
    {622, 60, 0},    // Eb5 (metallic edge)
    {348, 400, 0},   // F4 sustain (The "Doom Tone")
    {260, 400, 0},   // Drop
    {220, 800, 0},   // Tail fade
    {0, 0, 0}
};

// ==[ UI FEEDBACK SOUNDS ]==

// MODE_ENTER: Quick ascending pair - entering new mode
static const Note SND_MODE_ENTER[] = {
    {700, 30, 10},
    {1000, 40, 0},
    {0, 0, 0}
};

// MODE_EXIT: Quick descending pair - leaving mode
static const Note SND_MODE_EXIT[] = {
    {900, 30, 10},
    {600, 40, 0},
    {0, 0, 0}
};

// CONFIRM: Warm double tap - settings saved, action confirmed
static const Note SND_CONFIRM[] = {
    {800, 40, 15},
    {1100, 50, 0},
    {0, 0, 0}
};

// TYPING_KEY: Ultra-short keystroke tick
static const Note SND_TYPING_KEY[] = {
    {1200, 4, 0},
    {0, 0, 0}
};

// BACK_NAV: Soft descending - going back
static const Note SND_BACK_NAV[] = {
    {800, 25, 0},
    {0, 0, 0}
};

// ==[ PIG VOCALIZATIONS ]==
// Stepped pitch descent creates nasal "oink" quality (bfxr-inspired)

// ===[ DEMON WORDS ]===
// 2-step pitch contours in 280-1100 Hz range, ~80-120 ms total. Replaces
// the v2 OINK_* sequences. Each word has its own vowel character so the
// demon has a recognisable vocab, not just a tonal mood.

// ACK: bright snap - quick ascending double-tap, "yes?"
static const Note SND_ACK[] = {
    {820, 30, 5},
    {1080, 35, 0},
    {0, 0, 0}
};

// HEY: mid descending pair - casual call
static const Note SND_HEY[] = {
    {620, 35, 5},
    {460, 40, 0},
    {0, 0, 0}
};

// NAH: questioning upward - rising terminal intonation
static const Note SND_NAH[] = {
    {380, 28, 8},
    {520, 30, 5},
    {640, 35, 0},
    {0, 0, 0}
};

// MUM: soft murmur - low and warm, brief
static const Note SND_MUM[] = {
    {300, 35, 5},
    {360, 30, 5},
    {320, 30, 0},
    {0, 0, 0}
};

// OOF: short falling thud - mild disapproval
static const Note SND_OOF[] = {
    {560, 20, 0},
    {380, 28, 0},
    {260, 35, 0},
    {0, 0, 0}
};

// CUNT: blunt descending pair, ~150 ms total. Intentional - this is the
// default personality voice per the operator's direct instruction. Two
// distinct pitches so it doesn't blur into a generic low thud.
static const Note SND_CUNT[] = {
    {700, 25, 0},
    {420, 35, 0},
    {280, 45, 0},
    {0, 0, 0}
};

// Legacy OINK_* sequences kept as thin aliases - they still exist as
// SFX events but dispatch to the demon-word playback below. Old call
// sites (Mood::feed / Mood::eatWorld / Mood::pet) now use playPersonality
// directly so the demon's mood matches the configured voice word.

// ==[ AMBIENT SCANNING SOUNDS ]==

// SONAR_PING: Minimal single blip - periodic scan feedback
static const Note SND_SONAR_PING[] = {
    {1000, 20, 0},
    {0, 0, 0}
};

// RADAR_SWEEP: Subtle rising sweep - longer scan feedback
static const Note SND_RADAR_SWEEP[] = {
    {280, 30, 0},
    {350, 30, 0},
    {500, 30, 0},
    {700, 40, 0},
    {0, 0, 0}
};

// SCAN_TICK: Quiet periodic tick - background scanning
static const Note SND_SCAN_TICK[] = {
    {600, 8, 0},
    {0, 0, 0}
};

// ==[ AMBIENT BIRD SOUNDS ]==

// BIRD_HIT: Short electric zap - wave hits bird
static const Note SND_BIRD_HIT[] = {
    {1400, 25, 0},
    {900, 35, 0},
    {600, 20, 0},
    {0, 0, 0}
};

// BIRD_IMPACT: Low thud + crackle - bird hits ground
static const Note SND_BIRD_IMPACT[] = {
    {220, 60, 0},
    {350, 25, 10},
    {280, 20, 0},
    {0, 0, 0}
};

// THUNDER: Low boom (piezo "bass") + bright crack sizzle — same hybrid style
static const Note SND_THUNDER[] = {
    {300, 50, 0},     // chest boom (as low as piezo allows)
    {260, 40, 8},
    {900, 18, 0},     // crack
    {1400, 12, 0},    // sizzle tip
    {700, 20, 0},
    {400, 35, 0},     // rumble tail
    {0, 0, 0}
};

// WOLF: Low growl → short howl lift (menacing but short)
static const Note SND_WOLF[] = {
    {320, 45, 0},     // guttural start
    {280, 40, 8},     // growl down
    {360, 30, 0},
    {480, 50, 5},     // howl rise
    {520, 55, 0},     // sustain
    {400, 30, 0},     // fall off
    {0, 0, 0}
};

// JUMP: Cute boing — quick up then soft land (matches pig hop)
static const Note SND_JUMP[] = {
    {520, 20, 0},     // crouch push
    {780, 28, 0},     // spring up
    {980, 22, 6},     // peak
    {620, 18, 0},     // soft land
    {0, 0, 0}
};

// ATTACK_HOP: Punchy pounce — low kick + bright hit
static const Note SND_ATTACK_HOP[] = {
    {380, 25, 0},     // crouch load
    {520, 20, 0},     // launch
    {900, 18, 4},     // impact crack
    {650, 30, 0},     // body land
    {400, 35, 0},     // thud
    {0, 0, 0}
};

// WOLF_HIT: Pig lands a hit — short yelp + scamper (not full howl)
static const Note SND_WOLF_HIT[] = {
    {900, 18, 0},     // smack
    {720, 22, 4},     // yelp
    {1100, 20, 0},    // high flinch
    {480, 28, 0},     // scamper off
    {0, 0, 0}
};

// RAIN_TICK: very soft ambient drip — short, sparse, never spam-loud
static const Note SND_RAIN_TICK[] = {
    {1400, 12, 0},    // tiny drip
    {1100, 10, 0},    // soft second drop
    {0, 0, 0}
};

// IR_FIRE: short sci-fi charge + zap (play BEFORE mute / IR bitbang)
static const Note SND_IR_FIRE[] = {
    {400, 30, 0},     // charge hum
    {800, 25, 0},     // rise
    {1400, 18, 0},    // peak
    {2200, 22, 0},    // zap
    {900, 20, 0},     // decay
    {0, 0, 0}
};

// ==[ MORSE REMOVED ]==
// Morse GG was too long (600ms+), replaced with warm resolve in HANDSHAKE

// ==[ PER-SOUND VOLUME SCALING ]==
// Frequent/ambient sounds play quieter than celebrations.
// Schultz (1997): predicted events → subdued feedback; rare events → full punch.
static uint8_t currentVolumeScale = 100;  // 0-100%, set before each sound

static uint8_t eventVolumeScale(Event e) {
    switch (e) {
        // WHISPER (20%) — rain drip: quiet so it never tortures
        case RAIN_TICK:
            return 20;
        // AMBIENT (35%) — below conscious attention threshold
        case BIRD_HIT:
        case BIRD_IMPACT:
        case SCAN_TICK:
        case SONAR_PING:
            return 35;
        // FREQUENT (50%) — acknowledged but not alarming
        case TERMINAL_TICK:
        case NETWORK_NEW:
        case CLIENT_FOUND:
        case RADAR_SWEEP:
        case CLICK:
        case DEAUTH:
        case JUMP:
            return 50;
        case ATTACK_HOP:
            return 75;
        case IR_FIRE:
            return 80;
        // SCENE (70%) — thunder / wolf present but not full fanfare
        case THUNDER:
        case WOLF:
            return 70;
        case WOLF_HIT:
            return 65;
        // FULL (100%) — celebrations, captures, UI, pig voices
        default:
            return 100;
    }
}

// ==[ VOLUME MAPPING ]==
static void applyVolume() {
    // Cardputer piezo is LOUD — keep the whole curve low.
    // Level 1 = whisper (stealth), 3 = comfortable, 5 = noisy room.
    static const uint8_t kVolMap[] = {0, 20, 45, 80, 140, 210};
    uint8_t lvl = Config::personality().soundLevel;
    if (lvl > 5) lvl = 5;
    uint16_t vol = kVolMap[lvl];
    vol = (vol * currentVolumeScale) / 100;
    M5.Speaker.setVolume((uint8_t)vol);
}

// ==[ STATE MACHINE ]==
static const Note* currentSequence = nullptr;
static uint8_t currentStep = 0;
static uint32_t stepStartTime = 0;
static bool inNote = false;  // true = playing tone, false = in pause

// ==[ EVENT RING BUFFER ]== small queue; low-pri sounds do not stack
static constexpr uint8_t QUEUE_SIZE = 2;
static Event eventQueue[QUEUE_SIZE];
static volatile uint8_t queueHead = 0;  // next write position
static volatile uint8_t queueTail = 0;  // next read position
static portMUX_TYPE queueMutex = portMUX_INITIALIZER_UNLOCKED;
// Bitmask of MUTE_* reasons — play() only when zero
static volatile uint8_t s_muteMask = 0;

// ==[ IMPLEMENTATION ]==

static bool isPriorityEvent(Event event) {
    return event == PMKID || event == HANDSHAKE || event == ACHIEVEMENT ||
           event == LEVEL_UP || event == JACKPOT_XP || event == ULTRA_STREAK ||
           event == CHALLENGE_SWEEP || event == CHALLENGE_COMPLETE ||
           event == YOU_DIED || event == ERROR;
}

// Ambient / UI spam — never pile on top of an active sequence
static bool isLowPriorityEvent(Event event) {
    return event == NETWORK_NEW || event == DEAUTH || event == CLICK ||
           event == MENU_CLICK || event == TYPING_KEY || event == TERMINAL_TICK ||
           event == SONAR_PING || event == RADAR_SWEEP || event == SCAN_TICK ||
           event == RAIN_TICK || event == BIRD_HIT || event == BIRD_IMPACT ||
           event == JUMP || event == ATTACK_HOP || event == WOLF || event == WOLF_HIT ||
           event == MODE_ENTER || event == MODE_EXIT || event == BACK_NAV ||
           event == OINK_HAPPY || event == OINK_GRUNT || event == OINK_CURIOUS ||
           event == ACK || event == HEY || event == NAH ||
           event == MUM || event == OOF || event == CUNT ||
           event == THUNDER;
}

void init() {
    currentSequence = nullptr;
    currentStep = 0;
    queueHead = 0;
    queueTail = 0;
    s_muteMask = 0;
    
    // Initialize queue
    for (int i = 0; i < QUEUE_SIZE; i++) {
        eventQueue[i] = NONE;
    }
}

static void applyMuteMask(uint8_t mask) {
    s_muteMask = mask;
    if (mask != 0) stop();
}

void setMuted(bool muted) {
    // Legacy IR helper: only toggles MUTE_IR bit
    uint8_t m = s_muteMask;
    if (muted) m |= MUTE_IR;
    else       m = (uint8_t)(m & ~MUTE_IR);
    applyMuteMask(m);
}

void setScreenOffMuted(bool muted) {
    uint8_t m = s_muteMask;
    if (muted) m |= MUTE_SCREEN_OFF;
    else       m = (uint8_t)(m & ~MUTE_SCREEN_OFF);
    applyMuteMask(m);
}

bool isMuted() {
    return s_muteMask != 0;
}

uint8_t muteMask() {
    return s_muteMask;
}

void play(Event event) {
    if (s_muteMask != 0) return;
    if (Config::personality().soundLevel == 0) return;
    if (event == NONE) return;
    
    // Priority events (captures/celebrations) interrupt anything else
    bool isPriority = isPriorityEvent(event);
    if (isPriority && currentSequence != nullptr) {
        // Interrupt current sound for priority feedback
        M5.Speaker.stop();
        delayMicroseconds(150);  // settle audio driver before next tone
        currentSequence = nullptr;
        currentStep = 0;
        // Clear queue on priority
        taskENTER_CRITICAL(&queueMutex);
        queueHead = queueTail = 0;
        taskEXIT_CRITICAL(&queueMutex);
    } else if (isLowPriorityEvent(event)) {
        // Already playing or queued → drop (stops click/net/rain pile-up)
        taskENTER_CRITICAL(&queueMutex);
        bool busy = (currentSequence != nullptr) || (queueTail != queueHead);
        taskEXIT_CRITICAL(&queueMutex);
        if (busy) return;
    }
    
    // Enqueue event (ring buffer - drops oldest if full)
    taskENTER_CRITICAL(&queueMutex);
    uint8_t nextHead = (queueHead + 1) % QUEUE_SIZE;
    if (nextHead == queueTail) {
        // Buffer full - drop *new* low-pri, or drop oldest for priority
        if (!isPriority) {
            taskEXIT_CRITICAL(&queueMutex);
            return;
        }
        queueTail = (queueTail + 1) % QUEUE_SIZE;
    }
    eventQueue[queueHead] = event;
    queueHead = nextHead;
    taskEXIT_CRITICAL(&queueMutex);
}

static void startSequence(const Note* seq) {
    // Always stop residual speaker tone so notes never layer / feedback
    M5.Speaker.stop();
    delayMicroseconds(80);

    currentSequence = seq;
    currentStep = 0;
    stepStartTime = millis();
    inNote = true;

    // Apply current volume before starting playback
    applyVolume();

    // Start first note
    if (seq[0].freq > 0 && seq[0].duration > 0) {
        M5.Speaker.tone(seq[0].freq, seq[0].duration);
    }
}

bool update() {
    // Skip if muted or sound disabled
    if (s_muteMask != 0 || Config::personality().soundLevel == 0) {
        // Clear any queued events
        taskENTER_CRITICAL(&queueMutex);
        queueHead = queueTail;
        taskEXIT_CRITICAL(&queueMutex);
        currentSequence = nullptr;
        return false;
    }
    
    // Process queued event if nothing playing
    taskENTER_CRITICAL(&queueMutex);
    bool hasEvents = (queueTail != queueHead && currentSequence == nullptr);
    Event e = NONE;
    if (hasEvents) {
        e = eventQueue[queueTail];
        queueTail = (queueTail + 1) % QUEUE_SIZE;
    }
    taskEXIT_CRITICAL(&queueMutex);
    
    if (hasEvents) {
        currentVolumeScale = eventVolumeScale(e);
        switch (e) {
            case DEAUTH:
                startSequence(SND_DEAUTH);
                break;
            case HANDSHAKE:
                startSequence(SND_HANDSHAKE);
                break;
            case PMKID:
                startSequence(SND_PMKID);
                break;
            case NETWORK_NEW:
                startSequence(SND_NETWORK);
                break;
            case ACHIEVEMENT:
                startSequence(SND_ACHIEVEMENT);
                break;
            case LEVEL_UP:
                startSequence(SND_LEVEL_UP);
                break;
            case JACKPOT_XP:
                startSequence(SND_JACKPOT);
                break;
            case ULTRA_STREAK:
                startSequence(SND_ULTRA_STREAK);
                break;
            case CALL_RING:
                startSequence(SND_RING);
                break;
            case SYNC_COMPLETE:
                startSequence(SND_SYNC_COMPLETE);
                break;
            case ERROR:
                startSequence(SND_ERROR);
                break;
            case CLICK:
                startSequence(SND_CLICK);
                break;
            case MENU_CLICK:
                startSequence(SND_MENU_CLICK);
                break;
            case TERMINAL_TICK:
                {
                    static uint8_t termTickIndex = 0;
                    const Note* seq = SND_TERM_TICK_A;
                    switch (termTickIndex % 5) {
                        case 1: seq = SND_TERM_TICK_B; break;
                        case 2: seq = SND_TERM_TICK_C; break;
                        case 3: seq = SND_TERM_TICK_D; break;
                        case 4: seq = SND_TERM_TICK_E; break;
                        default: break;
                    }
                    termTickIndex++;
                    startSequence(seq);
                }
                break;
            case BOOT:
                startSequence(SND_BOOT);
                break;
            case PIGSYNC_BOOT:
                startSequence(SND_PIGSYNC_BOOT);
                break;
            case SIREN:
                startSequence(SND_SIREN);
                break;
            case CLIENT_FOUND:
                startSequence(SND_CLIENT_FOUND);
                break;
            case SIGNAL_LOST:
                startSequence(SND_SIGNAL_LOST);
                break;
            case CHANNEL_LOCK:
                startSequence(SND_CHANNEL_LOCK);
                break;
            case REVEAL_START:
                startSequence(SND_REVEAL_START);
                break;
            case CHALLENGE_COMPLETE:
                startSequence(SND_CHALLENGE_COMPLETE);
                break;
            case CHALLENGE_SWEEP:
                startSequence(SND_CHALLENGE_SWEEP);
                break;
            case YOU_DIED:
                startSequence(SND_YOU_DIED);
                break;
            // UI feedback
            case MODE_ENTER:
                startSequence(SND_MODE_ENTER);
                break;
            case MODE_EXIT:
                startSequence(SND_MODE_EXIT);
                break;
            case CONFIRM:
                startSequence(SND_CONFIRM);
                break;
            case TYPING_KEY:
                startSequence(SND_TYPING_KEY);
                break;
            case BACK_NAV:
                startSequence(SND_BACK_NAV);
                break;
            // Pig vocalizations (legacy aliases + demon words)
            case OINK_HAPPY:
            case ACK:
                startSequence(SND_ACK);
                break;
            case OINK_GRUNT:
            case HEY:
                startSequence(SND_HEY);
                break;
            case OINK_CURIOUS:
            case NAH:
                startSequence(SND_NAH);
                break;
            case MUM:
                startSequence(SND_MUM);
                break;
            case OINK_SQUEAL:
            case OOF:
                startSequence(SND_OOF);
                break;
            case CUNT:
                startSequence(SND_CUNT);
                break;
            // Ambient scanning
            case SONAR_PING:
                startSequence(SND_SONAR_PING);
                break;
            case RADAR_SWEEP:
                startSequence(SND_RADAR_SWEEP);
                break;
            case SCAN_TICK:
                startSequence(SND_SCAN_TICK);
                break;
            // Ambient birds
            case BIRD_HIT:
                startSequence(SND_BIRD_HIT);
                break;
            case BIRD_IMPACT:
                startSequence(SND_BIRD_IMPACT);
                break;
            // Scene FX
            case THUNDER:
                startSequence(SND_THUNDER);
                break;
            case WOLF:
                startSequence(SND_WOLF);
                break;
            case WOLF_HIT:
                startSequence(SND_WOLF_HIT);
                break;
            case IR_FIRE:
                startSequence(SND_IR_FIRE);
                break;
            case JUMP:
                startSequence(SND_JUMP);
                break;
            case ATTACK_HOP:
                startSequence(SND_ATTACK_HOP);
                break;
            case RAIN_TICK:
                startSequence(SND_RAIN_TICK);
                break;
            default:
                break;
        }
    }
    
    // Process current sequence
    if (currentSequence == nullptr) {
        taskENTER_CRITICAL(&queueMutex);
        bool eventsWaiting = (queueTail != queueHead);
        taskEXIT_CRITICAL(&queueMutex);
        return eventsWaiting;  // More events waiting?
    }
    
    uint32_t now = millis();
    const Note& note = currentSequence[currentStep];
    
    // Check if sequence ended (duration=0 marks end)
    if (note.duration == 0) {
        currentSequence = nullptr;
        currentStep = 0;
        taskENTER_CRITICAL(&queueMutex);
        bool eventsWaiting = (queueTail != queueHead);
        taskEXIT_CRITICAL(&queueMutex);
        return eventsWaiting;
    }
    
    if (inNote) {
        // In note phase - wait for duration
        if (now - stepStartTime >= note.duration) {
            // Note finished, enter pause phase
            inNote = false;
            stepStartTime = now;
            
            // If no pause, advance immediately
            if (note.pause == 0) {
                currentStep++;
                inNote = true;
                stepStartTime = now;
                
                const Note& next = currentSequence[currentStep];
                if (next.duration > 0 && next.freq > 0) {
                    M5.Speaker.stop();
                    delayMicroseconds(40);
                    M5.Speaker.tone(next.freq, next.duration);
                }
            }
        }
    } else {
        // In pause phase - wait for pause duration
        if (now - stepStartTime >= note.pause) {
            // Pause finished, advance to next note
            currentStep++;
            inNote = true;
            stepStartTime = now;
            
            const Note& next = currentSequence[currentStep];
            if (next.duration > 0 && next.freq > 0) {
                M5.Speaker.stop();
                delayMicroseconds(40);
                M5.Speaker.tone(next.freq, next.duration);
            }
        }
    }
    
    return true;
}

bool isPlaying() {
    taskENTER_CRITICAL(&queueMutex);
    bool playing = currentSequence != nullptr || (queueTail != queueHead);
    taskEXIT_CRITICAL(&queueMutex);
    return playing;
}

void stop() {
    currentSequence = nullptr;
    currentStep = 0;
    taskENTER_CRITICAL(&queueMutex);
    queueHead = queueTail = 0;
    taskEXIT_CRITICAL(&queueMutex);
    M5.Speaker.stop();
}

void tone(uint16_t freq, uint16_t duration) {
    if (s_muteMask != 0) return;
    if (Config::personality().soundLevel == 0) return;
    // Don't layer direct tones over an active sequence
    if (currentSequence != nullptr) return;
    applyVolume();
    M5.Speaker.tone(freq, duration);
}

void playPersonality() {
    // SOUND WORD settings row picks the demon word; if the operator
    // switched CUNT JINGLE on (default), the CUNT jingle fires instead.
    const PersonalityConfig& p = Config::personality();
    if (p.cuntJingle && p.voiceWord != (uint8_t)VOICE_CUNT) {
        play(CUNT);
        return;
    }
    switch ((VoiceWord)p.voiceWord) {
        case VOICE_HEY: play(HEY);  break;
        case VOICE_NAH: play(NAH);  break;
        case VOICE_MUM: play(MUM);  break;
        case VOICE_OOF: play(OOF);  break;
        case VOICE_CUNT: play(CUNT); break;
        case VOICE_ACK:
        default:         play(ACK);  break;
    }
}

void playCuntJingle() {
    play(CUNT);
}

}  // namespace SFX
